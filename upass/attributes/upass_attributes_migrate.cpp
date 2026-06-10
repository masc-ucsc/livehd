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

// (2b/E3e) aggregate-attr inheritance is a READ-side fallback now: a dotted
// lookup_attr_value miss falls back to the root's whole-bundle attr, so no
// pre-copies onto field paths are needed.

// (2b/E3c) migrate_alias deleted: attrs/shape/typed facts ride the shared
// binding slot; handlers are notified directly from on_assign_like.
