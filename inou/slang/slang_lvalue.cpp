//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Assignment-target lowering (todo/ 2s subtask B). Targets decompose the
// yosys-slang LValue::analyze way - variable, element/range select (through
// the same base+offset math as the rvalue side, onto set_mask), packed
// member access, concatenation (MSB-first split of the RHS), memory element
// write - instead of a fixed three-case switch.

#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/types/AllTypes.h"
#include "slang_context.hpp"

using slang::ast::ExpressionKind;

void Slang_context::note_write(const slang::ast::Symbol& sym, bool nonblocking, slang::SourceLocation loc) {
  if (proc_kind_ == Proc_kind::none) {
    return;
  }
  auto [it, inserted] = proc_assign_style_.try_emplace(&sym, Assign_style{nonblocking, loc});
  if (!inserted && it->second.nonblocking != nonblocking) {
    // yosys-slang's per-variable per-process style rule (procedural.cc).
    emit_error(slang::SourceRange(loc, loc),
               nonblocking ? "nonblocking-after-blocking" : "blocking-after-nonblocking",
               "type",
               std::string("variable '") + std::string(sym.name) + "' mixes blocking and non-blocking assignments",
               "use one assignment style per variable per process");
  }
  if (!nonblocking) {
    proc_blocking_written_.insert(&sym);
  }
}

void Slang_context::lower_assign(const slang::ast::AssignmentExpression& expr) {
  if (expr.timingControl != nullptr) {
    emit_warning(expr.timingControl->sourceRange, "intra-assign-timing", "unsupported",
                 "intra-assignment timing control is ignored (synthesis semantics)");
  }

  const auto& lhs = expr.left();

  // Compound assigns (a += b): slang represents the RHS as op(LValueReference, b).
  // Lower the current target value first so LValueReference can read it.
  std::string saved_compound = compound_read_;
  if (expr.isCompound()) {
    compound_read_ = lower_rvalue(lhs);  // the lhs re-read as an rvalue
  }

  auto rhs = to_int_value(lower_rvalue(expr.right()));

  compound_read_ = saved_compound;

  current_assign_nonblocking_ = expr.isNonBlocking();
  assign_to(lhs, rhs);
}

void Slang_context::assign_to(const slang::ast::Expression& lhs, const std::string& rhs) {
  switch (lhs.kind) {
    case ExpressionKind::NamedValue: {
      const auto& nv  = lhs.as<slang::ast::NamedValueExpression>();
      const auto& sym = nv.symbol;
      if (!declared_.contains(&sym) && !input_syms_.contains(&sym)) {
        declare_value_symbol(sym, /*force_reg=*/false);
      }
      auto name = lname_of(sym);
      if (rhs == name) {
        return;  // full-width self-assign (`q <= q;` hold idiom): an unwritten reg already holds
      }
      note_write(sym, current_assign_nonblocking_, lhs.sourceRange.start());
      builder_.create_assign_stmts(name, to_int_value(rhs));
      return;
    }

    case ExpressionKind::Conversion: {
      // slang wraps width-changing connections/LHS in conversions; a bitcast
      // of same bitstream width passes through to the inner target.
      const auto& conv = lhs.as<slang::ast::ConversionExpression>();
      assign_to(conv.operand(), rhs);
      return;
    }

    case ExpressionKind::Concatenation: {
      // {a, b} = rhs: first operand gets the MSBs.
      const auto& concat = lhs.as<slang::ast::ConcatenationExpression>();
      auto        ops    = concat.operands();
      int64_t     offset = 0;
      // walk LSB-first (reverse source order)
      for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        const auto& e  = **it;
        auto        oi = tinfo(*e.type);
        std::string part;
        if (offset == 0) {
          part = trunc_to(rhs, oi.bits);
        } else {
          part = trunc_to(builder_.create_sra_stmts(rhs, std::to_string(offset)), oi.bits);
        }
        if (oi.is_signed) {
          part = builder_.create_sext_stmts(part, std::to_string(oi.bits - 1));
        }
        assign_to(e, part);
        offset += oi.bits;
      }
      return;
    }

    case ExpressionKind::ElementSelect:
    case ExpressionKind::RangeSelect: {
      const auto& base = lhs.kind == ExpressionKind::ElementSelect ? lhs.as<slang::ast::ElementSelectExpression>().value()
                                                                   : lhs.as<slang::ast::RangeSelectExpression>().value();
      const auto& base_ty = base.type->getCanonicalType();

      if (base_ty.isUnpackedArray()) {
        lower_unpacked_write(lhs, rhs);
        return;
      }
      if (!base_ty.isIntegral() || !base_ty.hasFixedRange()) {
        emit_unsupported(lhs.sourceRange, "unsupported-lhs", "unsupported assignment-target shape");
        return;
      }

      // packed select write: read-modify-write through set_mask on the base.
      auto ti     = tinfo(*lhs.type);
      auto bi     = tinfo(base_ty);
      auto range  = base_ty.getFixedRange();
      int  stride = base_ty.isPackedArray() ? static_cast<int>(base_ty.getArrayElementType()->getBitWidth()) : 1;

      std::optional<int64_t> const_low;
      std::string            dyn_low;

      auto normalize = [&](const slang::ast::Expression& idx, int64_t width_down,
                           int64_t width_up) -> std::pair<std::optional<int64_t>, std::string> {
        if (auto ci = try_eval_int(idx)) {
          int64_t bottom = range.isDescending() ? (*ci - range.lower() - (width_down - 1))
                                                  : (range.upper() - *ci - (width_up - 1));
          return {bottom, {}};
        }
        auto v = lower_rvalue(idx);
        if (range.isDescending()) {
          int64_t bias = range.lower() + (width_down - 1);
          return {std::nullopt, bias == 0 ? v : builder_.create_minus_stmts(v, std::to_string(bias))};
        }
        int64_t bias = range.upper() - (width_up - 1);
        return {std::nullopt, builder_.create_minus_stmts(std::to_string(bias), v)};
      };

      if (lhs.kind == ExpressionKind::ElementSelect) {
        const auto& es               = lhs.as<slang::ast::ElementSelectExpression>();
        std::tie(const_low, dyn_low) = normalize(es.selector(), 1, 1);
      } else {
        const auto& rs = lhs.as<slang::ast::RangeSelectExpression>();
        using slang::ast::RangeSelectionKind;
        auto kind = rs.getSelectionKind();
        if (kind == RangeSelectionKind::Simple) {
          auto l = try_eval_int(rs.left());
          auto r = try_eval_int(rs.right());
          if (!l || !r) {
            emit_error(lhs.sourceRange, "non-const-range", "syntax", "simple range bounds must be compile-time constants");
            return;
          }
          const_low = range.isDescending() ? std::min(*l, *r) - range.lower() : range.upper() - std::max(*l, *r);
        } else {
          int64_t w = (ti.bits) / stride;
          if (kind == RangeSelectionKind::IndexedUp) {
            std::tie(const_low, dyn_low) = normalize(rs.left(), 1, w);
          } else {
            std::tie(const_low, dyn_low) = normalize(rs.left(), w, 1);
          }
        }
      }

      // The value written into the masked bits comes from the LOW bits of the
      // RHS; mask out a signed RHS pattern first.
      auto val = to_pattern(rhs, ti.bits, ti.is_signed);

      const auto* base_sym = resolve_base_symbol(base);
      if (base_sym == nullptr) {
        emit_unsupported(lhs.sourceRange, "unsupported-lhs-nesting",
                         "nested non-variable assignment targets are not supported yet");
        return;
      }
      auto base_name = lname_of(*base_sym);

      if (const_low) {
        int64_t lo_bit = *const_low * stride;
        if (lo_bit < 0) {
          emit_warning(lhs.sourceRange, "select-out-of-range", "bitwidth", "constant select is out of the declared range");
          lo_bit = 0;
        }
        std::string sel_mask = lo_bit == 0
                                   ? mask_text(ti.bits)
                                   : std::string(Dlop::get_mask_value(lo_bit + ti.bits - 1, lo_bit)->to_pyrope());
        note_write(*base_sym, current_assign_nonblocking_, lhs.sourceRange.start());
        builder_.create_set_mask_stmts(base_name, sel_mask, val);
        return;
      }

      // Dynamic select write: tolg requires const set_mask masks, so lower an
      // explicit read-modify-write with shifts (yosys-slang's shift-based
      // addressing, onto plain and/or/shl).
      auto cur   = read_symbol(*base_sym, base.sourceRange);
      auto cur_p = to_pattern(cur, bi.bits, bi.is_signed);

      std::string shamt = dyn_low;
      if (stride != 1) {
        shamt = builder_.create_mult_stmts(shamt, std::to_string(stride));
      }
      auto sel_mask = builder_.create_shl_stmts(mask_text(ti.bits), shamt);
      auto keep     = builder_.create_bit_and_stmts(cur_p, builder_.create_bit_not_stmts(sel_mask));
      auto ins      = builder_.create_shl_stmts(val, shamt);
      auto next     = builder_.create_bit_or_stmts({keep, ins});
      if (bi.is_signed) {
        next = builder_.create_sext_stmts(next, std::to_string(bi.bits - 1));
      }
      note_write(*base_sym, current_assign_nonblocking_, lhs.sourceRange.start());
      builder_.create_assign_stmts(base_name, next);
      return;
    }

    case ExpressionKind::MemberAccess: {
      const auto& ma = lhs.as<slang::ast::MemberAccessExpression>();
      if (ma.member.kind != slang::ast::SymbolKind::Field || !ma.value().type->isIntegral()) {
        emit_unsupported(lhs.sourceRange, "unsupported-lhs-member", "only packed-struct field targets are supported");
        return;
      }
      const auto& field = ma.member.as<slang::ast::FieldSymbol>();
      auto        ti    = tinfo(*lhs.type);
      auto        lo    = static_cast<int64_t>(field.bitOffset);
      std::string sel_mask
          = lo == 0 ? mask_text(ti.bits) : std::string(Dlop::get_mask_value(lo + ti.bits - 1, lo)->to_pyrope());
      auto        val      = to_pattern(rhs, ti.bits, ti.is_signed);
      const auto* base_sym = resolve_base_symbol(ma.value());
      if (base_sym == nullptr) {
        emit_unsupported(lhs.sourceRange, "unsupported-lhs-nesting",
                         "nested non-variable assignment targets are not supported yet");
        return;
      }
      note_write(*base_sym, current_assign_nonblocking_, lhs.sourceRange.start());
      builder_.create_set_mask_stmts(lname_of(*base_sym), sel_mask, val);
      return;
    }

    default:
      emit_unsupported(lhs.sourceRange, "unsupported-lhs",
                       std::string("assignment-target kind '") + std::string(slang::ast::toString(lhs.kind))
                           + "' is not supported by --reader slang yet");
  }
}

// Unpacked-array (memory) element access (2s-D). Reads lower to
// tuple_get(dst, mem, idx) = one read port per site; writes to the 3-child
// store(mem, idx, val) = one write port per site (enable = branch path
// condition, wired by tolg). Indices are biased by the declared lower bound.
std::string Slang_context::lower_unpacked_read(const slang::ast::Expression& expr) {
  if (expr.kind != ExpressionKind::ElementSelect) {
    emit_unsupported(expr.sourceRange, "unsupported-array-read",
                     "only single-element reads of unpacked arrays are supported by --reader slang");
    return "0";
  }
  const auto& es       = expr.as<slang::ast::ElementSelectExpression>();
  const auto* base_sym = resolve_base_symbol(es.value());
  if (base_sym != nullptr && flat_port_syms_.contains(base_sym)) {
    return flat_port_read(es, mem_info_.at(base_sym));
  }
  auto        mit      = base_sym != nullptr ? mem_info_.find(base_sym) : mem_info_.end();
  if (mit == mem_info_.end()) {
    emit_unsupported(expr.sourceRange, "unsupported-array-read", "unpacked array read on an unsupported base");
    return "0";
  }
  const auto& mi = mit->second;

  auto idx = to_int_value(lower_rvalue(es.selector()));
  if (mi.lower != 0) {
    idx = builder_.create_minus_stmts(idx, std::to_string(mi.lower));
  }

  auto& ln  = *builder_.lnast;
  auto  tg  = builder_.add_child(Lnast_ntype::create_tuple_get());
  auto  tmp = builder_.create_lnast_tmp();
  ln.add_child(tg, Lnast_node::create_ref(tmp));
  ln.add_child(tg, Lnast_node::create_ref(lname_of(*base_sym)));
  builder_.add_value_child_pub(tg, idx);

  if (mi.elem_signed) {
    return builder_.create_sext_stmts(tmp, std::to_string(mi.elem_bits - 1));
  }
  return tmp;
}

void Slang_context::lower_unpacked_write(const slang::ast::Expression& lhs, const std::string& rhs) {
  if (lhs.kind != ExpressionKind::ElementSelect) {
    emit_unsupported(lhs.sourceRange, "unsupported-array-write",
                     "only single-element writes of unpacked arrays are supported by --reader slang");
    return;
  }
  const auto& es       = lhs.as<slang::ast::ElementSelectExpression>();
  const auto* base_sym = resolve_base_symbol(es.value());
  if (base_sym != nullptr && flat_port_syms_.contains(base_sym)) {
    flat_port_write(es, mem_info_.at(base_sym), rhs);
    return;
  }
  auto        mit      = base_sym != nullptr ? mem_info_.find(base_sym) : mem_info_.end();
  if (mit == mem_info_.end()) {
    emit_unsupported(lhs.sourceRange, "unsupported-array-write", "unpacked array write on an unsupported base");
    return;
  }
  const auto& mi = mit->second;

  auto idx = to_int_value(lower_rvalue(es.selector()));
  if (mi.lower != 0) {
    idx = builder_.create_minus_stmts(idx, std::to_string(mi.lower));
  }
  auto val = to_pattern(to_int_value(rhs), mi.elem_bits, mi.elem_signed);

  note_write(*base_sym, current_assign_nonblocking_, lhs.sourceRange.start());

  auto& ln = *builder_.lnast;
  auto  st = builder_.add_child(Lnast_ntype::create_store());
  ln.add_child(st, Lnast_node::create_ref(lname_of(*base_sym)));
  builder_.add_value_child_pub(st, idx);
  builder_.add_value_child_pub(st, val);
}

// Unpacked-array PORT element access: the port is a FLAT packed bus, so
// element `arr[idx]` is the slice at bit `(idx-lower)*elem_bits` (reusing the
// packed-select shift/mask machinery), NOT a memory store/tuple_get.
std::string Slang_context::flat_port_read(const slang::ast::ElementSelectExpression& es, const Mem_info& mi) {
  const auto* base_sym  = resolve_base_symbol(es.value());
  const int   flat_bits = mi.elem_bits * static_cast<int>(mi.size);
  auto        p         = to_pattern(to_int_value(read_symbol(*base_sym, es.value().sourceRange)), flat_bits, false);

  if (auto ci = try_eval_int(es.selector())) {
    int64_t lo_bit = (*ci - mi.lower) * mi.elem_bits;
    if (lo_bit < 0 || lo_bit + mi.elem_bits > flat_bits) {
      emit_warning(es.sourceRange, "select-out-of-range", "bitwidth", "constant array-port select is out of range");
      lo_bit = std::max<int64_t>(lo_bit, 0);
    }
    auto r = extract_field(p, lo_bit, mi.elem_bits);
    return mi.elem_signed ? builder_.create_sext_stmts(r, std::to_string(mi.elem_bits - 1)) : r;
  }

  auto idx = to_int_value(lower_rvalue(es.selector()));
  if (mi.lower != 0) {
    idx = builder_.create_minus_stmts(idx, std::to_string(mi.lower));
  }
  std::string shamt = mi.elem_bits != 1 ? builder_.create_mult_stmts(idx, std::to_string(mi.elem_bits)) : idx;
  const int   bias  = mi.elem_bits;
  shamt             = builder_.create_plus_stmts(shamt, std::to_string(bias));
  auto shifted      = builder_.create_sra_stmts(builder_.create_shl_stmts(p, std::to_string(bias)), shamt);
  auto r            = trunc_to(shifted, mi.elem_bits);
  return mi.elem_signed ? builder_.create_sext_stmts(r, std::to_string(mi.elem_bits - 1)) : r;
}

void Slang_context::flat_port_write(const slang::ast::ElementSelectExpression& es, const Mem_info& mi,
                                    const std::string& rhs) {
  const auto* base_sym  = resolve_base_symbol(es.value());
  const int   flat_bits = mi.elem_bits * static_cast<int>(mi.size);
  auto        base_name = lname_of(*base_sym);
  auto        val       = to_pattern(to_int_value(rhs), mi.elem_bits, mi.elem_signed);

  if (auto ci = try_eval_int(es.selector())) {
    int64_t lo_bit = (*ci - mi.lower) * mi.elem_bits;
    if (lo_bit < 0 || lo_bit + mi.elem_bits > flat_bits) {
      emit_warning(es.sourceRange, "select-out-of-range", "bitwidth", "constant array-port select is out of range");
      lo_bit = std::max<int64_t>(lo_bit, 0);
    }
    std::string sel_mask = lo_bit == 0 ? mask_text(mi.elem_bits)
                                       : std::string(Dlop::get_mask_value(lo_bit + mi.elem_bits - 1, lo_bit)->to_pyrope());
    note_write(*base_sym, current_assign_nonblocking_, es.sourceRange.start());
    builder_.create_set_mask_stmts(base_name, sel_mask, val);
    return;
  }

  // dynamic element index: read-modify-write with shifts (const set_mask only).
  auto cur_p = to_pattern(to_int_value(read_symbol(*base_sym, es.value().sourceRange)), flat_bits, false);
  auto idx   = to_int_value(lower_rvalue(es.selector()));
  if (mi.lower != 0) {
    idx = builder_.create_minus_stmts(idx, std::to_string(mi.lower));
  }
  std::string shamt    = mi.elem_bits != 1 ? builder_.create_mult_stmts(idx, std::to_string(mi.elem_bits)) : idx;
  auto        sel_mask = builder_.create_shl_stmts(mask_text(mi.elem_bits), shamt);
  auto        keep     = builder_.create_bit_and_stmts(cur_p, builder_.create_bit_not_stmts(sel_mask));
  auto        ins      = builder_.create_shl_stmts(val, shamt);
  auto        next     = builder_.create_bit_or_stmts({keep, ins});
  note_write(*base_sym, current_assign_nonblocking_, es.sourceRange.start());
  builder_.create_assign_stmts(base_name, next);
}

// A select/member chain bottoms out in a named variable for the
// read-modify-write form; deeper nesting (select-of-select with dynamic
// indices) is diagnosed by the caller when this returns nullptr.
const slang::ast::ValueSymbol* Slang_context::resolve_base_symbol(const slang::ast::Expression& base) {
  if (base.kind == ExpressionKind::NamedValue) {
    const auto& sym = base.as<slang::ast::NamedValueExpression>().symbol;
    if (!declared_.contains(&sym) && !input_syms_.contains(&sym)) {
      declare_value_symbol(sym, /*force_reg=*/false);
    }
    return &sym;
  }
  if (base.kind == ExpressionKind::Conversion) {
    return resolve_base_symbol(base.as<slang::ast::ConversionExpression>().operand());
  }
  return nullptr;
}
