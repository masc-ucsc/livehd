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

#include "const.hpp"
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
// single quotes. Build an Const from a raw identifier text.
Const pyrope_string(std::string_view raw) {
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
      "ubits",
      "sbits",
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
      "key",
      "crand",
      "rand",
      "loc",
      "file",
      "type",
      "range",
      // Category B — LGraph wiring attrs
      "clock",
      "reset",
      "debug",
      "_debug",
      "async",
      "initial",
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

bool uPass_attributes::is_tuple(std::string_view var) const { return tuple_shapes.find(std::string{var}) != tuple_shapes.end(); }

const uPass_attributes::Tuple_shape* uPass_attributes::lookup_tuple_shape(std::string_view var) const {
  auto it = tuple_shapes.find(std::string{var});
  if (it == tuple_shapes.end()) {
    return nullptr;
  }
  return &it->second;
}

const uPass_attributes::Get_alias* uPass_attributes::lookup_get_alias(std::string_view tmp) const {
  auto it = tuple_get_alias.find(std::string{tmp});
  if (it == tuple_get_alias.end()) {
    return nullptr;
  }
  return &it->second;
}

void uPass_attributes::process_tuple_add() {
  // Layout (prp2lnast::tuple_to_node):
  //   tuple_add
  //     ref(dst)
  //     [ assign(ref(name), value) | const(value) | ref(value) ]+
  if (!move_to_child()) {
    return;
  }
  auto        dst = normalize_name(current_text());
  Tuple_shape shape;
  int         pos = 0;
  while (move_to_sibling()) {
    Tuple_field field;
    field.positional = std::to_string(pos);
    if (is_type(Lnast_ntype::Lnast_ntype_assign)) {
      // Named field: read the assign's first child for the key.
      if (move_to_child()) {
        field.name = std::string{current_text()};
        move_to_parent();
      }
    }
    shape.fields.push_back(std::move(field));
    ++pos;
  }
  move_to_parent();

  // Replace any existing shape for dst — tuple_add is the (re)build site.
  auto&      slot    = tuple_shapes[dst];
  const bool changed = slot.fields != shape.fields || slot.from_range != shape.from_range;
  slot               = std::move(shape);
  if (changed) {
    mark_changed();
  }

  // Phase 4 — once dst has a shape, eagerly materialize any aggregate cat-D
  // attrs onto each field's flattened path so downstream LGraph generation
  // sees them after the tuple shape is gone. attrs added later (e.g. an
  // attr_set on dst that follows the tuple_add) re-trigger from process_attr_set.
  migrate_aggregate_attrs_to_fields(dst);
}

void uPass_attributes::process_tuple_concat() {
  // Layout: ref(dst), (const|ref)... — each operand contributes its own
  // entries (a sub-tuple's fields for refs with a known shape; a single
  // positional slot for scalar consts/refs).
  if (!move_to_child()) {
    return;
  }
  auto        dst = normalize_name(current_text());
  Tuple_shape shape;
  int         pos                   = 0;
  auto        append_one_positional = [&]() {
    Tuple_field field;
    field.positional = std::to_string(pos++);
    shape.fields.push_back(std::move(field));
  };
  while (move_to_sibling()) {
    if (Lnast_ntype::is_const(get_raw_ntype())) {
      append_one_positional();
      continue;
    }
    if (!Lnast_ntype::is_ref(get_raw_ntype())) {
      // Unknown operand kind — bail; we cannot reason about size.
      shape.fields.clear();
      pos = 0;
      while (move_to_sibling()) {
      }
      break;
    }
    auto operand = normalize_name(current_text());
    if (auto* sub = lookup_tuple_shape(operand); sub) {
      for (const auto& f : sub->fields) {
        Tuple_field nf;
        nf.positional = std::to_string(pos++);
        nf.name       = f.name;
        shape.fields.push_back(std::move(nf));
      }
      continue;
    }
    // Ref to a non-tuple-typed operand: contributes a single positional slot.
    append_one_positional();
  }
  move_to_parent();

  if (!shape.fields.empty()) {
    auto&      slot    = tuple_shapes[dst];
    const bool changed = slot.fields != shape.fields || slot.from_range != shape.from_range;
    slot               = std::move(shape);
    if (changed) {
      mark_changed();
    }
  }
}

void uPass_attributes::process_tuple_set() {
  // Layout: ref(tuple), field..., value. We don't change the field count,
  // so re-touching shape is unnecessary; but for newly-introduced names we
  // could grow the shape. Keep it simple: leave shape alone.
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

  Get_alias alias;
  alias.base       = base;
  alias.field_key  = field_key;
  alias.field_name = field_key;  // default — will be overridden when positional → named

  // If field_key looks like a positional integer and base has a known shape,
  // resolve the positional to the source field's name (for `.[key]`).
  bool all_digits = !field_key.empty() && std::all_of(field_key.begin(), field_key.end(), [](char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
  });
  if (all_digits) {
    if (auto* sh = lookup_tuple_shape(base); sh) {
      auto idx = static_cast<size_t>(std::stoul(field_key));
      if (idx < sh->fields.size() && !sh->fields[idx].name.empty()) {
        alias.field_name = sh->fields[idx].name;
      }
    } else if (auto src_it = shape_source.find(std::string{base}); src_it != shape_source.end()) {
      if (auto* sh2 = lookup_tuple_shape(src_it->second); sh2) {
        auto idx = static_cast<size_t>(std::stoul(field_key));
        if (idx < sh2->fields.size() && !sh2->fields[idx].name.empty()) {
          alias.field_name = sh2->fields[idx].name;
        }
      }
    }
  }

  auto [it, inserted] = tuple_get_alias.emplace(dst, std::move(alias));
  if (!inserted) {
    if (it->second.base != alias.base || it->second.field_key != alias.field_key || it->second.field_name != alias.field_name) {
      it->second = std::move(alias);
      mark_changed();
    }
  } else {
    mark_changed();
  }
}

std::optional<Const> uPass_attributes::derive_aggregate_size(std::string_view base) const {
  if (auto* sh = lookup_tuple_shape(base); sh) {
    return *Dlop::create_integer(static_cast<int64_t>(sh->fields.size()));
  }
  // Aliased through `assign foo bar` where bar is shaped.
  if (auto it = shape_source.find(std::string{base}); it != shape_source.end()) {
    if (auto* sh = lookup_tuple_shape(it->second); sh) {
      return *Dlop::create_integer(static_cast<int64_t>(sh->fields.size()));
    }
  }
  // Range-typed: size = end - start + 1 (closed); end - start (open). Range
  // bounds in upass_attributes are recorded against the tmp produced by the
  // `range` op; if `base` is a name assigned from such a tmp, chain through
  // the assign-source.
  std::string range_key{base};
  if (auto it = shape_source.find(range_key); it != shape_source.end()) {
    range_key = it->second;
  }
  if (auto rb = lookup_range(range_key); rb) {
    if (rb->first.is_i() && rb->second.is_i()) {
      const auto sz = rb->second.to_i() - rb->first.to_i() + 1;
      if (sz > 0) {
        return *Dlop::create_integer(static_cast<int64_t>(sz));
      }
    }
  }
  return std::nullopt;
}

std::optional<Const> uPass_attributes::derive_aggregate_bits(std::string_view base) const {
  const Tuple_shape* sh = lookup_tuple_shape(base);
  std::string        bits_base{base};
  if (!sh) {
    if (auto it = shape_source.find(bits_base); it != shape_source.end()) {
      sh        = lookup_tuple_shape(it->second);
      bits_base = it->second;
    }
  }
  if (!sh) {
    return std::nullopt;
  }
  int64_t total = 0;
  for (const auto& f : sh->fields) {
    // Field path = "base.name" if named, else "base.positional".
    std::string field_path;
    field_path.reserve(base.size() + 1 + std::max(f.name.size(), f.positional.size()));
    field_path.assign(base.data(), base.size());
    field_path.push_back('.');
    field_path.append(f.name.empty() ? f.positional : f.name);

    auto bits = derive_bits(field_path, "bits");
    if (!bits || !bits->is_i()) {
      // Try the source-tmp form too (e.g. ___1.a) for fields not yet
      // re-keyed under the user's name.
      if (bits_base != std::string_view{base}) {
        std::string alt_path;
        alt_path.reserve(bits_base.size() + 1 + std::max(f.name.size(), f.positional.size()));
        alt_path.assign(bits_base);
        alt_path.push_back('.');
        alt_path.append(f.name.empty() ? f.positional : f.name);
        bits = derive_bits(alt_path, "bits");
      }
    }
    if (!bits || !bits->is_i()) {
      return std::nullopt;
    }
    total += bits->to_i();
  }
  return *Dlop::create_integer(total);
}

std::optional<Const> uPass_attributes::derive_aggregate_typename(std::string_view base, std::string_view base_text) const {
  // Explicit attr wins.
  if (auto v = lookup_attr_value(base, "typename"); v) {
    return v;
  }
  // Uppercase-named variable → its own name is the typename.
  if (is_uppercase_first(base) || is_uppercase_first(base_text)) {
    return pyrope_string(base);
  }
  return std::nullopt;
}

std::optional<Const> uPass_attributes::derive_aggregate_key(std::string_view base, std::string_view base_text) const {
  (void)base_text;
  // For an aggregate read on `var`, [key] returns the variable's own name.
  if (!base.empty()) {
    return pyrope_string(base);
  }
  return std::nullopt;
}

std::optional<Const> uPass_attributes::lookup_attr_with_inheritance(std::string_view base, std::string_view attr) const {
  // Direct lookup first.
  if (auto v = lookup_attr_value(base, attr); v) {
    return v;
  }
  // Tuple_get alias → look on the dotted path, then on the bare aggregate.
  if (auto* a = lookup_get_alias(base); a) {
    // 1. Try canonical aggregate's full path (e.g. "t.b").
    {
      std::string path = a->base + "." + a->field_key;
      if (auto v = lookup_attr_value(path, attr); v) {
        return v;
      }
      if (!a->field_name.empty() && a->field_name != a->field_key) {
        std::string alt = a->base + "." + a->field_name;
        if (auto v = lookup_attr_value(alt, attr); v) {
          return v;
        }
      }
    }
    // 2. Try via the shape-source tmp (e.g. "___1.b") for attrs recorded on
    //    the tuple-add tmp before the named-assign migrated the shape.
    if (auto it = shape_source.find(a->base); it != shape_source.end()) {
      std::string path = it->second + "." + a->field_key;
      if (auto v = lookup_attr_value(path, attr); v) {
        return v;
      }
      if (!a->field_name.empty() && a->field_name != a->field_key) {
        std::string alt = it->second + "." + a->field_name;
        if (auto v = lookup_attr_value(alt, attr); v) {
          return v;
        }
      }
    }
    // 3. Cat-D fallback: attr on the parent aggregate inherits down.
    if (!is_builtin_attr(attr)) {
      if (auto v = lookup_attr_value(a->base, attr); v) {
        return v;
      }
      if (auto it = shape_source.find(a->base); it != shape_source.end()) {
        if (auto v = lookup_attr_value(it->second, attr); v) {
          return v;
        }
      }
    }
  }
  // Bare base with a shape-source — also inherits from the source-tmp's
  // attrs (e.g. attrs landed on `___1` before the user-named alias).
  if (auto it = shape_source.find(std::string{base}); it != shape_source.end()) {
    if (auto v = lookup_attr_value(it->second, attr); v) {
      return v;
    }
  }
  // Phase 4 — chain through direct-ref aliases (`const v = t`,
  // `mut u = a`). Walk the chain so attrs landed at any level of the
  // aliasing hierarchy are visible to comptime reads on the surface name.
  // Bounded walk avoids any pathological loop introduced by mark_changed
  // re-running the pass on iterating constprop convergence.
  {
    std::string cursor{base};
    for (int hops = 0; hops < 8; ++hops) {
      auto it = direct_alias.find(cursor);
      if (it == direct_alias.end()) {
        break;
      }
      if (auto v = lookup_attr_value(it->second, attr); v) {
        return v;
      }
      // Also try the rhs's get_alias chain if the rhs itself was a tuple_get tmp.
      if (auto* a = lookup_get_alias(it->second); a) {
        if (!is_builtin_attr(attr)) {
          if (auto v = lookup_attr_value(a->base, attr); v) {
            return v;
          }
        }
      }
      cursor = it->second;
    }
  }
  return std::nullopt;
}
