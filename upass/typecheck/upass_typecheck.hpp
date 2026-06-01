//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "upass_core.hpp"

// uPass_typecheck — read-only KIND checker (see /Users/renau/projs/livehd/optable.md).
//
// Owns the `type`/`name` diagnostic categories: operator/operand kind
// homogeneity, `nil` poison, bool conditions, and no-type-change on writes. It
// NEVER looks at max/min or signedness — that is the bitwidth pass's job,
// enforced at the assignment via `wrap`/`sat`.
//
// There are ZERO implicit kind conversions: bool and int do NOT interoperate
// (`true ^ 0` is an error — write `int(true) ^ 0`); a variable's kind cannot
// change after it is established.
//
// Runs after `attributes`, before `constprop`. It re-infers scalar kinds itself
// (attributes' `Type_info` is private) via a `kind_map` side-map. Read-only:
// every hook leaves the tree untouched and the cursor balanced.
struct uPass_typecheck : public upass::uPass {
public:
  using upass::uPass::uPass;

  // Kind lattice (optable.md §"Scalar kinds"). `unknown` is the wildcard (skips
  // checks, never errors) — also the kind of a typeless single-unknown-bit
  // literal `0sb?`/`0ub?` (could be bool or int). `nil` is poison.
  enum class Kind : uint8_t { unknown, integer, boolean, string, range, tuple, nil };

  void begin_iteration() override { kind_map.clear(); }

  // Declarations record a variable's declared kind.
  void process_declare() override;
  void process_type_spec() override;
  // 2-child store: establish the dst kind, or reject a kind change.
  void process_assign() override;
  // if / elif / when / unless conditions must be boolean.
  void process_if() override;
  // while condition must be boolean.
  void process_while() override;

  // Arithmetic / bitwise / shift — int operands, int result.
  void process_plus() override;
  void process_minus() override;
  void process_mult() override;
  void process_div() override;
  void process_mod() override;
  void process_bit_and() override;
  void process_bit_or() override;
  void process_bit_xor() override;
  void process_bit_not() override;
  void process_shl() override;
  void process_sra() override;

  // Logical keywords — bool operands, bool result.
  void process_log_and() override;
  void process_log_or() override;
  void process_log_not() override;

  // Reductions / popcount — int operand; reductions → bool, popcount → int.
  void process_red_or() override;
  void process_red_and() override;
  void process_red_xor() override;
  void process_popcount() override;

  // Comparison — eq/ne (same class → bool), ordering (int → bool).
  void process_eq() override;
  void process_ne() override;
  void process_lt() override;
  void process_le() override;
  void process_gt() override;
  void process_ge() override;

  // Bit manipulation / type-id — result kinds only (operands not kind-checked).
  void process_get_mask() override;
  void process_set_mask() override;
  void process_sext() override;
  void process_is() override;

  // Aggregates — passthrough kinds, no homogeneity check.
  void process_range() override;
  void process_tuple_add() override;
  void process_tuple_get() override;
  void process_tuple_concat() override;

private:
  // Per-LNAST kind side-map (transient pass state — see MEMORY.md
  // "hhds_pass_local_attrs"). Keyed by var/tmp name.
  absl::flat_hash_map<std::string, Kind> kind_map;

  static std::string normalize_name(std::string_view n) { return std::string{n}; }
  static const char* kind_name(Kind k);
  static Kind        seed_kind_from_const(std::string_view literal_text);
  // Equality / assignment compatibility class: int / bool / string distinct;
  // range and tuple share a class (`range == flat tuple` is legal); -1 for
  // unknown/nil (no concrete class — the check is skipped).
  static int eq_class(Kind k);

  Kind kind_of(std::string_view name) const;
  void set_kind(std::string_view name, Kind k);
  Kind kind_of_operand_at_cursor();

  // Walk an op node (cursor ON the op): records child0 as `dst_name` and returns
  // the kinds of the remaining operands. Cursor is restored on return.
  std::vector<Kind> collect_operands(std::string& dst_name);

  // Scalar kind of a TYPE subtree at the cursor (prim_type_int/bool/string →
  // integer/boolean/string; else unknown). NO range reads.
  Kind kind_of_type_at_cursor();

  // `sym` is the source operator (for the message). `allow_nil` permits a nil
  // operand (the open-range `..` sentinel).
  void require_all(Kind required, Kind result, std::string_view sym, std::string_view code, bool allow_nil = false);
  void require_same(Kind result, std::string_view sym, std::string_view code);
  void stamp_result(Kind result);

  void emit_type_error(std::string_view code, const std::string& msg, std::string_view hint = {});
};
