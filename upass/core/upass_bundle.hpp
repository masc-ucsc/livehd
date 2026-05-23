//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Scaffolding types for the upass redesign (docs/upass_redesign.md).
//
// Status: types only — no pass yet builds an Operand_vec or writes a Vote.
// Migration plan in upass/upass.md §11 (steps 3-8). New passes can include
// this header today; the existing process_*/classify_statement/fold_ref
// surface in upass_core.hpp keeps the legacy migration on the old API until
// each pass is moved.

#include <cstdint>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "const.hpp"

namespace upass {

// ── Bundle ────────────────────────────────────────────────────────────────
//
// One Bundle per logical name. Lazily allocated — symbol-table entries
// default to Runtime_trivial; constprop alone installs Const; bundle pre-
// pass or attributes promotes to Bundle when *any* structural field needs
// a slot. See docs/upass_redesign.md §5.

enum class Decl_kind : uint8_t {
  unknown,
  mut_kind,
  const_kind,
  reg_kind,
  await_kind,
  let_kind,
};

enum class Numeric_kind : uint8_t {
  none,
  unsigned_int,
  signed_int,
  boolean,
  string,
};

// Placeholder until the import side lands (TODO_prp.md §1b).
struct ImportRef {
  std::string name;  // unresolved symbol; populated when import surfaces it
};

struct Tuple_field {
  std::string positional;  // "0", "1", …
  std::string name;        // user-visible field name, or empty for unnamed

  bool operator==(const Tuple_field& other) const {
    return positional == other.positional && name == other.name;
  }
};

struct Tuple_shape {
  std::vector<Tuple_field> fields;
  bool                     from_range{false};  // size derived from a range bound
};

struct Bundle {
  // ── Structural — written by bundle pre-pass ────────────────────────────
  Decl_kind    decl_kind   : 4 {Decl_kind::unknown};
  Numeric_kind num_kind    : 4 {Numeric_kind::none};
  bool         wrap_policy : 1 {false};
  bool         sat_policy  : 1 {false};
  bool         is_comptime : 1 {false};  // mirrors (value && !pending_import)
  uint16_t     bits{0};                  // 0 = unspecified
  uint16_t     assign_count{0};

  Tuple_shape                            shape;            // empty for scalars
  std::optional<std::pair<Const, Const>> range;            // start, end; populated by constprop
  absl::InlinedVector<std::string, 1>    sticky_taint;     // `_*` / `debug` flowed in
  absl::InlinedVector<ImportRef, 1>      pending_import;   // poison; sticky-style propagated

  // ── Value — written by constprop, narrowed by wrap/sat ─────────────────
  std::optional<Const> value;

  // ── Attribute values — written by attributes (filled by constprop) ────
  absl::flat_hash_map<std::string, Const> attr_values;

  // ── Aliasing — written by bundle pre-pass ──────────────────────────────
  std::shared_ptr<Bundle> direct_alias;          // `mut b = a` → b.direct_alias = a
  std::shared_ptr<Bundle> tuple_get_alias_base;  // tmp_dst from `tuple_get`
  std::string             tuple_get_alias_field;
};

// ── Operand vector ────────────────────────────────────────────────────────
//
// The runner pre-resolves each source node's operands into a small vector
// of {Runtime_trivial | Const | shared_ptr<Bundle>} and hands it to the
// passes. Passes never call move_to_child / scan_op. See §6.

struct Runtime_trivial {};  // explicit, self-documenting

using Operand     = std::variant<Runtime_trivial, Const, std::shared_ptr<Bundle>>;
using Operand_vec = absl::InlinedVector<Operand, 4>;

// ── Vote_state ────────────────────────────────────────────────────────────
//
// Per-source-node vote. After every opt pass has run, the runner reduces
// votes by priority: drop > toconst > update > keep. See §8.

struct Vote_state {
  enum class Kind : uint8_t {
    keep,     // emit the source op-kind verbatim (with operand folding)
    drop,     // emit nothing
    toconst,  // emit nothing; Bundle.value is committed for downstream
    update,   // emit the proposed shape stored in update_shape
  };

  Kind kind{Kind::keep};
  // Placeholder for the inline body (or other proposed rewrite). Concrete
  // representation lands when func_extract migration begins (§9).
  // Today: empty vector ⇒ no update payload.
  std::vector<uint8_t> update_shape;

  static constexpr Vote_state keep() { return {}; }
  static constexpr Vote_state drop() { return {Kind::drop, {}}; }
  static constexpr Vote_state toconst() { return {Kind::toconst, {}}; }
};

// ── Symbol table ──────────────────────────────────────────────────────────
//
// Per-name entry; lazily promoted from Runtime_trivial → Const → Bundle as
// passes discover state. See §4.

using Symbol_entry = std::variant<Runtime_trivial, Const, std::shared_ptr<Bundle>>;

class Symbol_table {
public:
  using Frame = absl::flat_hash_map<std::string, Symbol_entry>;

  Symbol_table() { frames_.emplace();  /* top-level frame */ }

  void push() { frames_.emplace(); }
  void pop() {
    // Every `{ }` pops with no merge in the no-if-arm case. If-arm merge
    // semantics live in the runner; the bare pop() drops locals (§4).
    if (frames_.size() > 1) {
      frames_.pop();
    }
  }
  std::size_t depth() const { return frames_.size(); }

  // Innermost → outermost lookup. Returns nullptr when not found.
  const Symbol_entry* lookup(std::string_view name) const {
    // std::stack has no random-access; copy aside to walk inner→outer.
    // Hot-path callers should cache lookups across operands of the same node.
    auto       stack_copy = frames_;
    while (!stack_copy.empty()) {
      const auto& frame = stack_copy.top();
      auto        it    = frame.find(std::string{name});
      if (it != frame.end()) {
        return &it->second;
      }
      stack_copy.pop();
    }
    return nullptr;
  }

  // Write to the top frame. A name's first definition pins it to its scope;
  // subsequent assigns at the same level mutate the same Bundle (§4).
  void set(std::string_view name, Symbol_entry entry) {
    frames_.top().insert_or_assign(std::string{name}, std::move(entry));
  }

private:
  std::stack<Frame> frames_;
};

}  // namespace upass
