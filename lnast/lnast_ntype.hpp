//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Static-only utility over the Lnast node-type enum. The enum value is stored
// directly in the HHDS tree slot via Lnast::set_type/get_type. Detached
// Lnast_node values only represent ref/const/invalid leaves; structural nodes
// should use this raw type value directly.
//
// Encoding invariant: bit 0 of the underlying value is reserved for
// is_loop_last, matching Ntype_op in graph/cell.hpp. Current LNAST nodes are
// all non-loop-last and therefore use even values; future loop-breaking LNAST
// nodes should use the odd slot next to their non-loop-last form.
class Lnast_ntype {
private:
  static constexpr int lnast_ntype_counter_base = __COUNTER__;

public:
  enum Lnast_ntype_int : uint8_t {
#define LNAST_NODE(NAME, VERBAL)      Lnast_ntype_##NAME = (__COUNTER__ - lnast_ntype_counter_base - 1) * 2,
#define LNAST_LOOP_NODE(NAME, VERBAL) Lnast_ntype_##NAME = ((__COUNTER__ - lnast_ntype_counter_base - 1) * 2) | 1,
#include "lnast_nodes.def"
    Lnast_ntype_last_invalid
  };

private:
  inline constexpr static auto namemap = []() {
    std::array<std::string_view, Lnast_ntype_last_invalid> a{};
    for (auto& s : a) {
      s = "invalid";
    }
#define LNAST_NODE(NAME, VERBAL)      a[static_cast<size_t>(Lnast_ntype_##NAME)] = #VERBAL;
#define LNAST_LOOP_NODE(NAME, VERBAL) a[static_cast<size_t>(Lnast_ntype_##NAME)] = #VERBAL;
#include "lnast_nodes.def"
    return a;
  }();

public:
  Lnast_ntype()                              = delete;
  Lnast_ntype(const Lnast_ntype&)            = delete;
  Lnast_ntype& operator=(const Lnast_ntype&) = delete;

  // Per-type creator + predicate.
#define LNAST_NODE(NAME, VERBAL)                                                  \
  static constexpr Lnast_ntype_int create_##NAME() { return Lnast_ntype_##NAME; } \
  static constexpr bool            is_##NAME(Lnast_ntype_int v) { return v == Lnast_ntype_##NAME; }
#define LNAST_LOOP_NODE(NAME, VERBAL)                                             \
  static constexpr Lnast_ntype_int create_##NAME() { return Lnast_ntype_##NAME; } \
  static constexpr bool            is_##NAME(Lnast_ntype_int v) { return v == Lnast_ntype_##NAME; }
#include "lnast_nodes.def"

  static constexpr bool is_tuple_attr(Lnast_ntype_int v) {
    return v == Lnast_ntype_tuple_add || v == Lnast_ntype_tuple_get || v == Lnast_ntype_attr_set || v == Lnast_ntype_attr_get;
  }

  // Super types
  static constexpr bool is_primitive_op(Lnast_ntype_int v) { return v >= Lnast_ntype_dp_assign && v <= Lnast_ntype_attr_get; }
  static constexpr bool is_logical_op(Lnast_ntype_int v) {
    return v == Lnast_ntype_log_and || v == Lnast_ntype_log_or || v == Lnast_ntype_log_not;
  }
  static constexpr bool is_unary_op(Lnast_ntype_int v) {
    return v == Lnast_ntype_bit_not || v == Lnast_ntype_log_not || v == Lnast_ntype_store || v == Lnast_ntype_dp_assign;
  }
  static constexpr bool is_binary_op(Lnast_ntype_int v) {
    return v == Lnast_ntype_shl || v == Lnast_ntype_sra || v == Lnast_ntype_sext;
  }
  static constexpr bool is_nary_op(Lnast_ntype_int v) {
    return v == Lnast_ntype_bit_and || v == Lnast_ntype_bit_or || v == Lnast_ntype_bit_xor || v == Lnast_ntype_log_and
           || v == Lnast_ntype_log_or || v == Lnast_ntype_plus || v == Lnast_ntype_minus || v == Lnast_ntype_mult
           || v == Lnast_ntype_is || v == Lnast_ntype_eq || v == Lnast_ntype_ne || v == Lnast_ntype_lt || v == Lnast_ntype_le
           || v == Lnast_ntype_gt || v == Lnast_ntype_ge;
  }

  // basic_op have 1-to-1 translation between LNAST and Lgraph
  static constexpr bool is_direct_lgraph_op(Lnast_ntype_int v) {
    return (v >= Lnast_ntype_bit_and && v <= Lnast_ntype_ge) && v != Lnast_ntype_mod && v != Lnast_ntype_is && v != Lnast_ntype_ne
           && v != Lnast_ntype_le && v != Lnast_ntype_ge;
  }

  static constexpr bool is_type(Lnast_ntype_int v) { return v >= Lnast_ntype_prim_type_none && v <= Lnast_ntype_comp_type_lambda; }
  static constexpr bool is_loop_last(Lnast_ntype_int v) { return (static_cast<uint8_t>(v) & 1) != 0; }

  static std::string_view to_sv(Lnast_ntype_int v) { return namemap[v]; }
  static std::string_view debug_name(Lnast_ntype_int v) { return namemap[v]; }

  static_assert(namemap.size() == Lnast_ntype_last_invalid);
  static_assert((static_cast<uint8_t>(Lnast_ntype_invalid) & 1) == 0);
  static_assert((static_cast<uint8_t>(Lnast_ntype_top) & 1) == 0);
  static_assert((static_cast<uint8_t>(Lnast_ntype_comp_type_lambda) & 1) == 0);
};
