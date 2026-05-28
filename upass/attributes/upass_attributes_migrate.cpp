//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Per-field attribute migration when tuples expand / arrays lower.
//
// The structural lowering (tuple_add → field-vars, mixed-type split,
// multi-D flatten) is constprop's job. What lives here is the attribute
// pass's contribution:
//
//   * **User-attr migration.** When fields are split out, any category-D
//     aggregate attribute that was not overridden at the field site must end
//     up on the field's flattened name so LGraph generation still sees it.
//     This file materializes that propagation eagerly into
//     `attr_set_values[base.fieldname]` once a name acquires a tuple shape
//     (via tuple_add or via direct alias from a shaped rhs).
//
//   * **Alias chaining.** Direct-ref aliases (`const v = t.a`,
//     `mut u = t`) must continue the cat-D inheritance lookup chain. The
//     alias rhs's tuple_get_alias / shape_source / direct_alias entries
//     are copied to the lhs so a later `lhs.[attr]` resolves through the
//     same path the rhs would have used.
//
// Aggregate attrs themselves stay on the aggregate name (cat-D inheritance
// is defined as "fields read the aggregate's value when not overridden",
// so the aggregate slot remains the source of truth). Per-field
// materialization writes copies for downstream LGraph generation; reads
// still resolve through `lookup_attr_with_inheritance` which prefers the
// explicit field entry when present.

#include <optional>
#include <string>
#include <string_view>

#include "const.hpp"
#include "upass_attributes.hpp"

void uPass_attributes::migrate_aggregate_attrs_to_fields(std::string_view base) {
  // Lookup the shape. Both tuple_shapes and shape_source can carry a shape
  // for `base`; tuple_shapes wins, shape_source is the fallback.
  const Tuple_shape* sh = lookup_tuple_shape(base);
  if (!sh) {
    auto src_it = shape_source.find(std::string{base});
    if (src_it != shape_source.end()) {
      sh = lookup_tuple_shape(src_it->second);
    }
  }
  if (!sh) {
    return;
  }

  auto agg_it = attr_set_values.find(std::string{base});
  if (agg_it == attr_set_values.end()) {
    return;
  }

  // Field-level overrides for `base` may live on the source-tmp's per-field
  // path (e.g. `___1.b` carries `poison=99` because the per-field decl-site
  // attr_set fired against the tuple_get tmp before the named assign moved
  // shape onto `base`). Resolve those once so we don't clobber a field's
  // override with the aggregate value.
  auto              src_it   = shape_source.find(std::string{base});
  const std::string src_base = (src_it != shape_source.end()) ? src_it->second : std::string{};

  auto field_override = [&](const std::string& key, const std::string& attr_name) -> std::optional<Const> {
    if (src_base.empty()) {
      return std::nullopt;
    }
    std::string p;
    p.reserve(src_base.size() + 1 + key.size());
    p.assign(src_base);
    p.push_back('.');
    p.append(key);
    return lookup_attr_value(p, attr_name);
  };

  for (const auto& [attr_name, attr_value] : agg_it->second) {
    // Built-in attrs have dedicated semantics; don't propagate them as
    // generic cat-D inheritance. (`[debug]` and sticky `_*` attrs are
    // tracked by the sticky handler and don't live in attr_set_values
    // for the field path.)
    if (is_builtin_attr(attr_name)) {
      continue;
    }
    for (const auto& f : sh->fields) {
      const std::string& key = f.name.empty() ? f.positional : f.name;
      if (key.empty()) {
        continue;
      }
      // Resolve the value to write: a per-field override on the source-tmp
      // wins; otherwise the aggregate's value flows down.
      Const chosen_value = attr_value;
      if (auto ov_named = field_override(key, attr_name); ov_named) {
        chosen_value = *ov_named;
      } else if (!f.positional.empty() && f.positional != key) {
        if (auto ov_pos = field_override(f.positional, attr_name); ov_pos) {
          chosen_value = *ov_pos;
        }
      }

      // Write `base.<suffix>`'s `attr_name` slot, marking dirty only when
      // the stored value actually changed. The aggregate-flow value may
      // overwrite an earlier migration when the source-tmp's override turns
      // out to be more authoritative.
      auto write_field_attr = [&](std::string_view suffix) {
        std::string field_path;
        field_path.reserve(base.size() + 1 + suffix.size());
        field_path.assign(base.data(), base.size());
        field_path.push_back('.');
        field_path.append(suffix);
        auto& slot = attr_set_values[field_path];
        auto  it   = slot.find(attr_name);
        if (it == slot.end()) {
          slot.emplace(attr_name, chosen_value);
          mark_changed();
        } else if (!it->second.same_repr(chosen_value)) {
          it->second = chosen_value;
          mark_changed();
        }
      };
      write_field_attr(key);
      // Mirror the positional path too (so reads via positional index find
      // the inherited attr). Skip when name == positional.
      if (!f.name.empty() && f.name != f.positional && !f.positional.empty()) {
        write_field_attr(f.positional);
      }
    }
  }
}

void uPass_attributes::migrate_alias(std::string_view lhs, std::string_view rhs) {
  if (lhs.empty() || rhs.empty() || lhs == rhs) {
    return;
  }
  const std::string lhs_s{lhs};
  const std::string rhs_s{rhs};

  // 1. Record the direct alias edge so future lookups can chain.
  {
    auto [it, inserted] = direct_alias.emplace(lhs_s, rhs_s);
    if (!inserted && it->second != rhs_s) {
      it->second = rhs_s;
      mark_changed();
    } else if (inserted) {
      mark_changed();
    }
  }

  // 2. If the rhs is itself a tuple_get tmp, propagate the alias mapping so
  //    `lhs.[attr]` resolves the same way rhs would. Without this,
  //    `const v = t.a` (lowered as `tuple_get ___N t a; assign v ___N`)
  //    would lose the inheritance link from v to t.
  if (auto* a = lookup_get_alias(rhs); a) {
    auto [it, inserted] = tuple_get_alias.emplace(lhs_s, *a);
    if (!inserted) {
      if (it->second.base != a->base || it->second.field_key != a->field_key || it->second.field_name != a->field_name) {
        it->second = *a;
        mark_changed();
      }
    } else {
      mark_changed();
    }
  }

  // 3. Eagerly materialize the rhs's direct attrs onto the lhs (without
  //    overwriting any explicit lhs attr). This covers the scalar-alias
  //    case `const y::[foo=1] = a` where a already carries `foo` — the
  //    attr is now visible under both names for downstream consumers.
  auto rhs_it = attr_set_values.find(rhs_s);
  if (rhs_it != attr_set_values.end()) {
    auto& lhs_slot = attr_set_values[lhs_s];
    for (const auto& [attr_name, attr_value] : rhs_it->second) {
      auto existing = lhs_slot.find(attr_name);
      if (existing == lhs_slot.end()) {
        lhs_slot.emplace(attr_name, attr_value);
        mark_changed();
      }
    }
  }

  // 4. Phase 8 typesystem: when the rhs is a tuple tmp whose fields have
  //    recorded type_info (via per-field `tuple_get` + `attr_set
  //    ubits/sbits` from prp2lnast), migrate the per-field type_info
  //    onto the lhs.<field> paths so `lhs.field.[bits]` reads resolve
  //    without walking the alias chain at lookup time.
  for (const auto& [tmp, alias] : tuple_get_alias) {
    if (alias.base != rhs_s) {
      continue;
    }
    auto ti_it = type_info_map.find(tmp);
    if (ti_it == type_info_map.end() || !ti_it->second.has_type_spec) {
      continue;
    }
    const auto& field = alias.field_name.empty() ? alias.field_key : alias.field_name;
    if (field.empty()) {
      continue;
    }
    std::string field_path;
    field_path.reserve(lhs_s.size() + 1 + field.size());
    field_path = lhs_s;
    field_path.push_back('.');
    field_path += field;
    auto [it, inserted] = type_info_map.emplace(std::move(field_path), ti_it->second);
    if (inserted) {
      mark_changed();
    } else if (it->second.bits != ti_it->second.bits || it->second.kind != ti_it->second.kind) {
      it->second = ti_it->second;
      mark_changed();
    }
  }
}
