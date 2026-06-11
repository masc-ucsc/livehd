//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Aggregate (tuple/array) shape tracking.
//
// process_tuple_add/concat/set/get build the structural side state that lets
// aggregate attribute reads (`tup.[size]`, `tup.[bits]`, `tup.[typename]`,
// `tup.[key]`) and category-D aggregate→field inheritance resolve before
// downstream lowering flattens the tuple away.
//
// Per-field attr materialization that pairs with this state lives in
// upass_attributes_migrate.cpp.
//
// Side state populated here:
//   * tuple_shapes[var]     — list of (positional, name) per field
//   * tuple_get_alias[tmp]  — tmp ref produced by `tuple_get` resolved to
//                             (base, field_key, field_name) so attr_get on
//                             the tmp can find the underlying aggregate's
//                             attrs and return `.[key]`.
//   * shape_source[var]     — when `assign var src` runs and src has a
//                             shape, var inherits the shape; the source-tmp
//                             is recorded so attribute lookups can chain
//                             through alias names too.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hlop/dlop.hpp"
#include "lnast_ntype.hpp"
#include "upass_attributes.hpp"

namespace {

bool is_uppercase_first(std::string_view s) {
  if (s.empty()) {
    return false;
  }
  return !s.empty() && std::isupper(static_cast<unsigned char>(s.front())) != 0;
}

// String-quote helper: Dlop::from_pyrope expects strings with surrounding
// single quotes. Build an Dlop from a raw identifier text.
Dlop pyrope_string(std::string_view raw) {
  std::string s;
  s.reserve(raw.size() + 2);
  s.push_back('\'');
  s.append(raw.data(), raw.size());
  s.push_back('\'');
  return *Dlop::from_pyrope(s);
}

}  // namespace

bool uPass_attributes::is_builtin_attr(std::string_view name) {
  // Category A/B/C built-ins. All other names are category-D (user / unknown)
  // and inherit from aggregate to field.
  static constexpr std::string_view names[] = {
      // Category A — LNAST/upass attrs
      "max",
      "min",
      "bits",
      "wrap",
      "saturate",
      "sat",
      "comptime",
      "const",
      "mut",
      "typename",
      "private",
      "size",
      "sign",
      "key",
      "crand",
      "rand",
      "loc",
      "file",
      "type",
      // Category B — LGraph wiring attrs
      "clock",
      "reset",
      "debug",
      "_debug",
      "async",
      "init",
      "clock_pin",
      "din",
      "enable",
      "negreset",
      "posclk",
      "reset_pin",
      "valid",
      "stop",
      "lat",
      "num",
      "addr",
      "fwd",
      "wensize",
      "rdport",
      "defer",
      "inputs",
      "outputs",
      // Category C — synthesis hints
      "critical",
      "delay",
      "donttouch",
      "keep",
      "inp_delay",
      "out_delay",
      "max_delay",
      "min_delay",
      "max_load",
      "max_fanout",
      "max_cap",
      "left_of",
      "right_of",
      "top_of",
      "bottom_of",
      "align_with",
  };
  return std::find(std::begin(names), std::end(names), name) != std::end(names);
}

upass::Vote uPass_attributes::process_tuple_add(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  (void)dst_name;
  (void)dst;
  (void)src;
  // Layout (prp2lnast::tuple_to_node):
  //   tuple_add
  //     ref(dst)
  //     [ assign(ref(name), value) | const(value) | ref(value) ]+
  // No shape recording: the constructed bundle IS the shape
  // (derive_aggregate_size / derive_comptime walk the binding's top levels).
  return upass::Vote::keep;
}

upass::Vote uPass_attributes::process_tuple_concat(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  (void)dst_name;
  (void)dst;
  (void)src;
  // Layout: ref(dst), (const|ref)... — each operand contributes its own
  // entries (a sub-tuple's fields for refs with a known shape; a single
  // positional slot for scalar consts/refs).
  // No shape recording (see process_tuple_add).
  return upass::Vote::keep;
}

void uPass_attributes::process_tuple_set() {
  // Layout: ref(tuple), field..., value. `tuple_set t p... v` writes through
  // the root `t`, so the const-rebind tally must count this write — otherwise
  // a producer's choice of `tuple_set` for what is structurally `t.p... = v`
  // bypasses the `assign`-only check in record_assign (todo/ 1e audit).
  // A `nil` value is an invalidation, not a binding — same policy as assign.
  if (!move_to_child()) {
    return;
  }
  auto        target     = normalize_name(current_text());
  bool        rhs_is_nil = false;
  std::string first_field;
  while (move_to_sibling()) {
    if (is_last_child()) {
      if (current_text() == "nil") {
        rhs_is_nil = true;
      }
      break;
    }
    if (first_field.empty()) {
      first_field = std::string(current_text());
    }
  }
  move_to_parent();

  // A write through an explicitly-`mut` field is the FIELD's binding, not a
  // root rebind: `const t = (mut a:u4=1, …); t.a += 3` is legal (the const
  // applies to the binding of t, mut fields stay mutable — 04-variables.md).
  // prp2lnast records the marker as Decl_kind::mut_kind on the field path
  // (declare on the field's tuple_get tmp; lookup_type_info alias-chases).
  if (!first_field.empty()) {
    std::string field_path;
    field_path.reserve(target.size() + 1 + first_field.size());
    field_path = target;
    field_path.push_back('.');
    field_path += first_field;
    if (const auto* fti = lookup_type_info(field_path); fti != nullptr && fti->decl == Decl_kind::mut_kind) {
      return;
    }
  }

  record_assign(target, rhs_is_nil);
}

void uPass_attributes::process_tuple_get() {
  // Layout (prp2lnast::dot_expression_to_node, attribute path lowerings):
  //   tuple_get
  //     ref(dst)
  //     ref(base)
  //     (const|ref)(field)
  //
  // We record an alias mapping dst → (base, field_key, field_name) so a
  // subsequent attr_get on dst can resolve cat-D inheritance and `.[key]`.
  if (!move_to_child()) {
    return;
  }
  auto dst = normalize_name(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto base = normalize_name(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  std::string field_key;
  if (Lnast_ntype::is_const(get_raw_ntype())) {
    field_key = std::string{current_text()};
  } else if (Lnast_ntype::is_ref(get_raw_ntype())) {
    // Runtime/symbolic field — we cannot canonicalize without a constant.
    // Drop alias rather than recording a fragile entry.
    move_to_parent();
    return;
  }
  // Multi-level path: walk siblings; the canonical alias for chained
  // `t.a.b` is recorded against the final dst with a dotted key.
  while (move_to_sibling()) {
    if (Lnast_ntype::is_const(get_raw_ntype())) {
      field_key += '.';
      field_key += current_text();
    } else {
      // Symbolic continuation — bail out.
      field_key.clear();
      break;
    }
  }
  move_to_parent();

  if (field_key.empty()) {
    return;
  }

  std::string field_name = field_key;  // overridden when positional → named

  // If field_key looks like a positional integer and base has a known shape,
  // resolve the positional to the source field's name (for `.[key]`).
  bool all_digits = !field_key.empty() && std::all_of(field_key.begin(), field_key.end(), [](char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
  });
  if (all_digits && runner_st != nullptr) {
    if (const auto b = runner_st->get_bundle(base); b) {
      const auto idx = static_cast<int>(std::stoul(field_key));
      for (const auto& tl : b->top_levels()) {
        if (tl.pos == idx && !tl.name.empty()) {
          field_name = std::string(tl.name);
          break;
        }
      }
    }
  }

  // Record the resolved (possibly positional→named) origin for
  // `.[key]` and the typed-fact back-flow (Symbol_table::tget_origin).
  if (runner_st != nullptr && !field_name.empty()) {
    runner_st->tget_origin.insert_or_assign(dst, base + "." + field_name);
  }
}

std::optional<Dlop> uPass_attributes::derive_aggregate_size(std::string_view base) const {
  // Size = the binding's top-level cardinality (aliases share the
  // slot; constprop materializes closed integer ranges as tuple bundles, so
  // ranges resolve here too).
  if (runner_st != nullptr && base.find('.') == std::string_view::npos) {
    if (const auto b = runner_st->get_bundle(base); b) {
      const size_t n = b->named_top_count() + b->unnamed_top_count();
      if (n > 0 && !(n == 1 && b->is_scalar())) {
        return *Dlop::create_integer(static_cast<int64_t>(n));
      }
    }
  }
  // Range bounds recorded against the `range` op's tmp; chain through the
  // alias source recorded at `assign var range_tmp` (range_source).
  std::string range_key{base};
  if (auto it = range_source.find(range_key); it != range_source.end()) {
    range_key = it->second;
  }
  if (auto rb = lookup_range(range_key); rb) {
    if (rb->first.is_integer() && rb->second.is_integer()) {
      // size = end - start + 1 (bundle cardinality), in Dlop arithmetic.
      auto sz = rb->second.sub_op(rb->first)->add_op(*Dlop::create_integer(1));
      if (!sz->is_negative() && !sz->is_known_zero()) {
        return *sz;
      }
    }
  }
  return std::nullopt;
}

std::optional<Dlop> uPass_attributes::derive_aggregate_bits(std::string_view base) const {
  // Phase 8 typesystem redesign: aggregates (tuples / arrays) have no
  // `.[bits]`. Only scalars do — per-field queries `t.a.[bits]` are
  // handled by derive_bits, not here. The earlier sum-of-fields rule
  // is intentionally removed (see todo/ task 1v corpus rewrite).
  (void)base;
  return std::nullopt;
}

std::optional<Dlop> uPass_attributes::derive_aggregate_typename(std::string_view base, std::string_view base_text) const {
  // Explicit attr wins.
  if (auto v = lookup_attr_value(base, "typename"); v) {
    return v;
  }
  // Uppercase-named variable → its own name is the typename.
  if (is_uppercase_first(base) || is_uppercase_first(base_text)) {
    return pyrope_string(base);
  }
  // Task 1t — named-type FIELD typename. A typed field `inn:inner_t` records its
  // typename on a `tuple_get` tmp (see prp2lnast typed-field lowering); resolve
  // base = "X.inn" (or a tuple_get tmp aliasing it) to that tmp and read its
  // typename. The container X is searched directly, through `direct_alias`, and
  // through X's OWN named type — so `o.inn` where `o:outer_t` reaches
  // `outer_t.inn`'s field typename. Mirrors lookup_type_info for numeric types.
  std::string parent;
  std::string field;
  if (runner_st != nullptr) {
    if (auto oit = runner_st->tget_origin.find(std::string(base)); oit != runner_st->tget_origin.end()) {
      parent = std::string(Bundle::get_first_level(oit->second));
      field  = std::string(Bundle::get_all_but_first_level(oit->second));
    }
  }
  if (parent.empty()) {
    if (auto dot = base.rfind('.'); dot != std::string_view::npos) {
      parent = std::string{base.substr(0, dot)};
      field  = std::string{base.substr(dot + 1)};
    } else {
      return std::nullopt;
    }
  }
  if (field.empty()) {
    return std::nullopt;
  }
  // The field typename rides the container binding's field attr
  // (set_binding_attr echoes extraction-tmp attr_sets to the source field;
  // aliases share the slot, so no alias hop / sibling-tmp scan).
  auto field_typename = [&](const std::string& container) -> std::optional<Dlop> {
    return lookup_attr_value(container + "." + field, "typename");
  };
  // 1. The field typename recorded directly on `parent`'s bundle.
  if (auto v = field_typename(parent); v) {
    return v;
  }
  // 2. Via parent's own named type (`o:outer_t` → `outer_t.inn`).
  if (auto pt = derive_aggregate_typename(parent, parent); pt) {
    auto        repr      = pt->to_pyrope();
    std::string type_name = (repr.size() >= 2 && repr.front() == '\'' && repr.back() == '\'')
                                ? std::string{repr.substr(1, repr.size() - 2)}
                                : std::string{repr};
    if (auto v = field_typename(type_name); v) {
      return v;
    }
  }
  return std::nullopt;
}

std::optional<Dlop> uPass_attributes::derive_aggregate_key(std::string_view base, std::string_view base_text) const {
  (void)base_text;
  // For an aggregate read on `var`, [key] returns the variable's own name.
  if (!base.empty()) {
    return pyrope_string(base);
  }
  return std::nullopt;
}

std::optional<Dlop> uPass_attributes::lookup_attr_with_inheritance(std::string_view base, std::string_view attr) const {
  // Attrs ride the bindings (shared slots, value copies, the
  // name-fact preservation in Symbol_table::set), and lookup_attr_value
  // already does the cat-D field→root fallback. The one chase left is an
  // EXTRACTION TMP base: look on its source field path (which falls back to
  // the source aggregate for cat-D attrs).
  if (auto v = lookup_attr_value(base, attr); v) {
    return v;
  }
  if (runner_st != nullptr) {
    if (auto oit = runner_st->tget_origin.find(std::string(base)); oit != runner_st->tget_origin.end()) {
      if (auto v = lookup_attr_value(oit->second, attr); v) {
        return v;
      }
    }
  }
  return std::nullopt;
}
