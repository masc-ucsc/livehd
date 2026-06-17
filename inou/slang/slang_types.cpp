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

std::string Slang_context::int_max_str(int bits, bool is_signed) const {
  if (bits <= 0) {
    bits = 1;
  }
  if (!is_signed) {
    return std::string(Dlop::get_mask_value(bits)->to_pyrope());  // 2^bits - 1
  }
  // signed max = 2^(bits-1) - 1; a 1-bit signed maxes at 0 (range {-1,0}).
  // get_mask_value(0) returns 1 (narrow-arg wart), so special-case bits<=1.
  if (bits <= 1) {
    return "0";
  }
  return std::string(Dlop::get_mask_value(bits - 1)->to_pyrope());
}

std::string Slang_context::int_min_str(int bits, bool is_signed) const {
  if (!is_signed) {
    return "0";
  }
  if (bits <= 0) {
    bits = 1;
  }
  // signed min = -2^(bits-1). get_neg_mask_value(arg) returns the correct
  // -2^arg for arg >= 2, but +1 for arg <= 1 (the narrow wart), so the two
  // narrow widths are special-cased; bits >= 3 (arg >= 2) delegates and stays
  // exact even past 64 bits.
  if (bits == 1) {
    return "-1";
  }
  if (bits == 2) {
    return "-2";
  }
  return std::string(Dlop::get_neg_mask_value(bits - 1)->to_pyrope());
}

void Slang_context::emit_prim_type_int(const Lnast_nid& parent, int bits, bool is_signed) {
  auto& ln = *builder_.lnast;
  auto  ty = ln.add_child(parent, Lnast_ntype::create_prim_type_int());
  ln.add_child(ty, Lnast_node::create_const(int_max_str(bits, is_signed)));
  ln.add_child(ty, Lnast_node::create_const(int_min_str(bits, is_signed)));
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

  // signed -> unsigned (to_bits >= from_bits here; the narrowing case is handled
  // above). Reinterpret the source's bits as unsigned at its OWN width, giving a
  // non-negative value; widening to the larger to_bits is then a zero-extend
  // (automatic in unbounded-integer semantics). Masking to to_bits instead would
  // SIGN-extend the signed source (get_mask replicates the sign bit past
  // from_bits) — wrong, e.g. a signed 4'b1000 (-8) in an unsigned context is 8,
  // not 24. This is the Verilog rule that an unsigned expression treats a signed
  // operand as unsigned for width extension (1800 §11.8.2).
  return trunc_to(v, from_bits);
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
