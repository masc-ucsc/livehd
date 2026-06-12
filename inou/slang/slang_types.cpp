//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Type mapping + the single materialize-conversion seam (todo/ 2s subtask A).
// LNAST is infinite-precision integer semantics: a lowered value always sits
// in the range of its slang type, so widening is free and only truncation /
// sign-reinterpretation emit nodes. CIRCT's materializeConversion is the
// model: resize first (extension kind picked by SOURCE signedness), then
// domain - here "domain" collapses to the signed/unsigned reinterpret.

#include "hlop/dlop.hpp"
#include "iassert.hpp"
#include "slang/ast/types/AllTypes.h"
#include "slang_context.hpp"

Slang_context::Tinfo Slang_context::tinfo(const slang::ast::Type& t) {
  Tinfo ti;
  ti.bits      = static_cast<int>(t.getBitWidth());
  ti.is_signed = t.isSigned();
  if (ti.bits <= 0) {
    ti.bits = 1;  // non-numeric types reach here only on already-diagnosed paths
  }
  return ti;
}

std::string Slang_context::mask_text(int bits) const { return std::string(Dlop::get_mask_value(bits)->to_pyrope()); }

void Slang_context::emit_prim_type_int(const Lnast_nid& parent, int bits, bool is_signed) {
  auto& ln = *builder_.lnast;
  auto  ty = ln.add_child(parent, Lnast_ntype::create_prim_type_int());
  if (is_signed) {
    ln.add_child(ty, Lnast_node::create_const(Dlop::get_mask_value(bits - 1)->to_pyrope()));
    ln.add_child(ty, Lnast_node::create_const(Dlop::get_neg_mask_value(bits - 1)->to_pyrope()));
  } else {
    ln.add_child(ty, Lnast_node::create_const(Dlop::get_mask_value(bits)->to_pyrope()));
    ln.add_child(ty, Lnast_node::create_const("0"));
  }
}

std::string Slang_context::trunc_to(const std::string& v, int bits) {
  if (bits <= 1) {
    return builder_.create_bit_and_stmts(v, "1");
  }
  return builder_.create_get_mask_stmts(v, mask_text(bits));
}

std::string Slang_context::extract_field(const std::string& v, int64_t lo, int bits) {
  if (lo <= 0) {
    return trunc_to(v, bits);
  }
  // get_mask both selects and shifts down, but its single-bit form is the
  // -1/0 boolean; shift+trunc keeps the verilog 0/1 value for any width.
  if (bits == 1) {
    return builder_.create_bit_and_stmts(builder_.create_sra_stmts(v, std::to_string(lo)), "1");
  }
  return builder_.create_get_mask_stmts(v, Dlop::get_mask_value(static_cast<int>(lo) + bits - 1, static_cast<int>(lo))->to_pyrope());
}

std::string Slang_context::materialize_conversion(const std::string& v, int from_bits, bool from_signed, int to_bits,
                                                  bool to_signed) {
  I(!v.empty());
  if (to_bits <= 0) {
    return v;
  }

  if (to_bits < from_bits) {
    // Truncate: keep the low to_bits of the two's-complement pattern...
    auto masked = trunc_to(v, to_bits);
    if (!to_signed) {
      return masked;
    }
    // ...and reinterpret the top kept bit as the sign when the target is signed.
    return builder_.create_sext_stmts(masked, std::to_string(to_bits - 1));
  }

  if (from_signed == to_signed) {
    return v;  // widening (or same width) preserves the value in integer semantics
  }

  if (!from_signed && to_signed) {
    // unsigned -> signed: only a same-width reinterpret can flip the value.
    if (to_bits == from_bits) {
      return builder_.create_sext_stmts(v, std::to_string(to_bits - 1));
    }
    return v;
  }

  // signed -> unsigned: negative values wrap to the to_bits pattern.
  return trunc_to(v, to_bits);
}

std::string Slang_context::to_pattern(const std::string& v, int bits, bool is_signed) {
  if (!is_signed) {
    return v;
  }
  return trunc_to(v, bits);
}

std::string Slang_context::fit_wrap(const std::string& v, int bits, bool is_signed) {
  auto masked = trunc_to(v, bits);
  if (!is_signed) {
    return masked;
  }
  return builder_.create_sext_stmts(masked, std::to_string(bits - 1));
}
