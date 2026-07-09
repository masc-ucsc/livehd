//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Assignment-target lowering (todo/ 2s subtask B). Targets decompose the
// yosys-slang LValue::analyze way - variable, element/range select (through
// the same base+offset math as the rvalue side, onto set_mask), packed
// member access, concatenation (MSB-first split of the RHS), memory element
// write - instead of a fixed three-case switch.

#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/types/AllTypes.h"

#include "absl/strings/str_cat.h"
#include "slang_context.hpp"

using slang::ast::ExpressionKind;

namespace {
// An lvalue assignment-pattern element on an OUTPUT-port connection arrives
// wrapped as `<target> = EmptyArgument` (the same shape lower_instance peels off
// the whole connection). Strip the wrapper to the real target lvalue.
const slang::ast::Expression& pattern_lvalue_target(const slang::ast::Expression& e) {
  if (const auto* a = e.as_if<slang::ast::AssignmentExpression>()) {
    return a->left();
  }
  return e;
}
}  // namespace

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

  // Whole-struct write to a per-field bundle var (`io = '{...}'` / `io = other`):
  // split into one leaf write per field. Done BEFORE lowering the RHS so a
  // struct literal's per-field values bind directly to each leaf — NOT a re-slice
  // of the concatenated whole (which would make `io.result` depend on the
  // concat's `io.operation` read, reintroducing the false self-loop).
  if (!expr.isCompound()) {
    const slang::ast::Expression* l = &lhs;
    while (l->kind == ExpressionKind::Conversion) {
      l = &l->as<slang::ast::ConversionExpression>().operand();
    }
    if (l->kind == ExpressionKind::NamedValue
        && (is_scalar_struct_var(l->as<slang::ast::NamedValueExpression>().symbol)
            || is_packed_array_bundle_var(l->as<slang::ast::NamedValueExpression>().symbol))) {
      current_assign_nonblocking_ = expr.isNonBlocking();
      if (assign_struct_whole(l->as<slang::ast::NamedValueExpression>().symbol, expr.right())) {
        return;
      }
    }
  }

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
      // A bundle-declared struct var has NO flat net — every read resolves
      // through its leaves, so a flat store would be dead (undriven leaves,
      // dropped instance-output bindings: the _vsetModule_io_out/ExeUnitImp_4
      // family). Split the already-lowered value onto the leaves instead.
      if (assign_struct_whole_value(sym, rhs, lhs.sourceRange.start())) {
        return;
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

      // An element/range select on an unpacked array is a memory/flat-port
      // write (single-element only); handled separately.
      if (base_ty.isUnpackedArray()) {
        lower_unpacked_write(lhs, rhs);
        return;
      }

      // `regs[idx] <= data` of a memory-ized packed 2-D reg (register file): a
      // whole-ELEMENT write routes to the memory write path even though the base
      // is a packed array. Only a single-element select (`regs[idx]`, not a
      // sub-bit/range select of an element) — those keep the bit-slice RMW path.
      if (lhs.kind == ExpressionKind::ElementSelect && base.kind != ExpressionKind::ElementSelect
          && base.kind != ExpressionKind::RangeSelect) {
        const auto* base_sym = resolve_base_symbol(base);
        if (base_sym != nullptr && !flat_port_syms_.contains(base_sym) && mem_info_.contains(base_sym)
            && packed_mem_regs_.contains(base_sym)) {
          lower_unpacked_write(lhs, rhs);
          return;
        }
      }

      // `mem[addr][const-chunk] <= data`: a chunked masked memory write (the
      // base is a packed element of an unpacked array). Lowered via the memory
      // write-enable-granularity (wensize) path before the packed-root attempt.
      if (lower_mem_element_bitslice_write(lhs, rhs)) {
        return;
      }

      // `mem[addr][dyn-bit/slice] <= data` with a NON-constant in-word position
      // (e.g. `useful[i][decrBit] <= 0`): no constant chunk to enable, so
      // read-modify-write the addressed word.
      if (lower_mem_element_dynamic_write(lhs, rhs)) {
        return;
      }

      // A whole-element write `vec[const] = v` into a per-element bundle array
      // routes to the element's own leaf net (a per-element bundle has no flat
      // whole-array net for resolve_packed_lvalue to root the RMW on).
      if (lhs.kind == ExpressionKind::ElementSelect && base.kind == ExpressionKind::NamedValue) {
        const auto& bsym = base.as<slang::ast::NamedValueExpression>().symbol;
        if (is_packed_array_bundle_var(bsym)) {
          const auto& es = lhs.as<slang::ast::ElementSelectExpression>();
          if (auto ci = try_eval_int(es.selector())) {
            const auto& bt  = bsym.getType().getCanonicalType();
            auto        rng = bt.getFixedRange();
            int64_t     idx = rng.isDescending() ? (*ci - rng.lower()) : (rng.upper() - *ci);
            if (!declared_.contains(&bsym)) {
              declare_value_symbol(bsym, /*force_reg=*/false);
            }
            if (auto it = struct_var_info_.find(&bsym); it != struct_var_info_.end()) {
              if (const auto* f = find_struct_field(it->second, absl::StrCat("e", idx))) {
                note_write(bsym, current_assign_nonblocking_, lhs.sourceRange.start());
                emit_leaf_store(absl::StrCat(lname_of(bsym), ".", f->name),
                                fit_wrap(to_int_value(rhs), f->bits, f->is_signed));
                return;
              }
            }
          }
        }
      }

      // Any chain of packed `.field` / `[idx]` / `[hi:lo]` collapses to one
      // contiguous bit-slice of a root variable (handles arbitrary nesting,
      // e.g. `bus[i].field`, `s.sub.arr[j]`).
      Packed_lv lv;
      if (resolve_packed_lvalue(lhs, lv)) {
        emit_packed_rmw(lv, rhs, lhs.sourceRange);
        return;
      }
      if (!base_ty.isIntegral() || !base_ty.hasFixedRange()) {
        emit_unsupported(lhs.sourceRange, "unsupported-lhs", "unsupported assignment-target shape");
        return;
      }
      emit_unsupported(lhs.sourceRange, "unsupported-lhs-nesting",
                       "nested non-variable assignment targets are not supported yet");
      return;
    }

    case ExpressionKind::MemberAccess: {
      const auto& ma = lhs.as<slang::ast::MemberAccessExpression>();
      if (ma.member.kind != slang::ast::SymbolKind::Field || !ma.value().type->isIntegral()) {
        emit_unsupported(lhs.sourceRange, "unsupported-lhs-member", "only packed-struct field targets are supported");
        return;
      }
      // Field write into a struct-element (tuple) memory: `mem[idx].field <= v`.
      // Lower to a field store on the tuple memory (detuple -> store(mem.field,
      // idx, v)); a memory base would otherwise fall through to the unsupported
      // nested-lvalue diagnostic (resolve_packed_lvalue rejects a memory base).
      if (ma.value().kind == ExpressionKind::ElementSelect) {
        const auto& es       = ma.value().as<slang::ast::ElementSelectExpression>();
        const auto* base_sym = resolve_base_symbol(es.value());
        if (base_sym != nullptr && !flat_port_syms_.contains(base_sym)) {
          auto mit = mem_info_.find(base_sym);
          if (mit != mem_info_.end() && mit->second.is_tuple) {
            const auto& mi    = mit->second;
            const auto& field = ma.member.as<slang::ast::FieldSymbol>();
            if (const auto* f = find_tuple_field(mi, field.name)) {
              auto idx = to_int_value(lower_rvalue(es.selector()));
              if (mi.lower != 0) {
                idx = builder_.create_minus_stmts(idx, std::to_string(mi.lower));
              }
              note_write(*base_sym, current_assign_nonblocking_, lhs.sourceRange.start());
              emit_field_store(lname_of(*base_sym), idx, f->name, to_pattern(to_int_value(rhs), f->bits, f->is_signed));
              return;
            }
          }
        }
      }
      // Field write of a scalar packed-struct VARIABLE lowered as a bundle:
      // `io.operation = v` is a plain scalar write of the leaf net.
      if (ma.value().kind == ExpressionKind::NamedValue) {
        const auto& bsym = ma.value().as<slang::ast::NamedValueExpression>().symbol;
        if (is_scalar_struct_var(bsym)) {
          if (!declared_.contains(&bsym)) {
            declare_value_symbol(bsym, /*force_reg=*/false);
          }
          const auto& field = ma.member.as<slang::ast::FieldSymbol>();
          if (auto it = struct_var_info_.find(&bsym); it != struct_var_info_.end()) {
            if (const auto* f = find_struct_field(it->second, field.name)) {
              note_write(bsym, current_assign_nonblocking_, lhs.sourceRange.start());
              // fit_wrap (truncate + sign-reinterpret), not to_pattern: the leaf
              // is declared at the field's width/sign, so a signed field must land
              // in its signed range (to_pattern would leave an unsigned pattern).
              auto val = fit_wrap(to_int_value(rhs), f->bits, f->is_signed);
              if (it->second.is_tuple) {
                emit_struct_field_set(lname_of(bsym), f->name, val);  // tuple field store
              } else {
                emit_leaf_store(absl::StrCat(lname_of(bsym), ".", f->name), val);  // flat leaf
              }
              return;
            }
          }
        }
      }
      Packed_lv lv;
      if (resolve_packed_lvalue(lhs, lv)) {
        emit_packed_rmw(lv, rhs, lhs.sourceRange);
        return;
      }
      emit_unsupported(lhs.sourceRange, "unsupported-lhs-nesting",
                       "nested non-variable assignment targets are not supported yet");
      return;
    }

    case ExpressionKind::SimpleAssignmentPattern:
      // `'{a, b, ...}` used as an assignment target. The only SV-legal form is an
      // unpacked-array output-port connection (the port type supplies the
      // pattern's context), e.g. soomrv's `.OUT_idx('{resLzTz})`.
      assign_to_pattern(lhs, lhs.as<slang::ast::SimpleAssignmentPatternExpression>().elements(), rhs);
      return;

    default:
      emit_unsupported(lhs.sourceRange, "unsupported-lhs",
                       std::string("assignment-target kind '") + std::string(slang::ast::toString(lhs.kind))
                           + "' is not supported by --reader slang yet");
  }
}

// `'{...}` (SimpleAssignmentPattern) as an assignment target. slang lowers an
// unpacked-array output-port connection `.p('{a, b})` to `'{a, b} = <child
// output>`, so `rhs` is the child's flattened output bus. Decompose it per
// element and recurse:
//   - unpacked array target: source element k maps to array index left+k*step
//     (LRM: first listed element -> leftmost index); its bit offset in the flat
//     bus is (idx-lower)*elem_bits, exactly matching flat_port_read/write
//     (Verilator/yosys flatten unpacked ports identically, so LEC lines up).
//   - packed (integral) target: slang resolves elements MSB-first, like a
//     `{...}` concatenation (element 0 occupies the high bits).
// Whole-struct write to a per-field bundle var. Each field's leaf net gets its
// OWN driver value (a struct literal's per-field expression, a sibling struct's
// matching leaf, or — for any other packed RHS — a slice of that whole value).
bool Slang_context::assign_struct_whole(const slang::ast::ValueSymbol& sym, const slang::ast::Expression& rhs) {
  if (!declared_.contains(&sym)) {
    declare_value_symbol(sym, /*force_reg=*/false);
  }
  auto it = struct_var_info_.find(&sym);
  if (it == struct_var_info_.end()) {
    return false;
  }
  // SNAPSHOT the struct info: the lower_rvalue()/declare_value_symbol() calls
  // below can declare further structs and INSERT into struct_var_info_, rehashing
  // it and dangling any reference into it. A dangling `si.fields.size()` re-read
  // each loop iteration over-ran `elems` (observed SIGABRT on DstMgu). Copy what
  // we need up front. (Same flat_hash_map rehash-invalidation class as the
  // tuple_slot_ref bug.)
  const bool is_tuple = it->second.is_tuple;
  const auto fields    = it->second.fields;  // copy
  auto       base      = lname_of(sym);
  // Write field `f` of this struct: a wire struct is a real tuple (field-store
  // op, detuple-split); a mut struct is flat leaves.
  auto put = [&](const std::string& field, const std::string& value) {
    if (is_tuple) {
      emit_struct_field_set(base, field, value);
    } else {
      emit_leaf_store(absl::StrCat(base, ".", field), value);
    }
  };

  const slang::ast::Expression* r = &rhs;
  while (r->kind == ExpressionKind::Conversion) {
    r = &r->as<slang::ast::ConversionExpression>().operand();
  }

  // `io = '{...}` assignment pattern: slang resolves elements in field order, so
  // element[i] is field[i]'s value. Write each leaf from its element directly.
  std::span<const slang::ast::Expression* const> elems;
  bool                                           is_pattern = true;
  switch (r->kind) {
    case ExpressionKind::SimpleAssignmentPattern:
      elems = r->as<slang::ast::SimpleAssignmentPatternExpression>().elements();
      break;
    case ExpressionKind::StructuredAssignmentPattern:
      elems = r->as<slang::ast::StructuredAssignmentPatternExpression>().elements();
      break;
    case ExpressionKind::ReplicatedAssignmentPattern:
      elems = r->as<slang::ast::ReplicatedAssignmentPatternExpression>().elements();
      break;
    default: is_pattern = false; break;
  }
  if (is_pattern && elems.size() == fields.size()) {
    note_write(sym, current_assign_nonblocking_, rhs.sourceRange.start());
    for (size_t i = 0; i < fields.size(); ++i) {
      const auto& f = fields[i];
      // fit_wrap (not to_pattern): land each value in the leaf's declared
      // width/sign — a signed field must stay in its signed range.
      auto v = fit_wrap(to_int_value(lower_rvalue(*elems[i])), f.bits, f.is_signed);
      put(f.name, v);
    }
    return true;
  }

  // `arr = {e_hi, ..., e_lo}` per-element bit-concatenation driving an ARRAY
  // bundle: operand[i] is element field[i] (both MSB-first — a concat's first
  // operand is the high bits = highest element index = fields[0]). Only when every
  // operand is exactly one element wide and the count matches (the CIRCT
  // shift-network shape); otherwise fall through to the whole-value slice below.
  if (r->kind == ExpressionKind::Concatenation && is_packed_array_bundle_var(sym)) {
    const auto ops = r->as<slang::ast::ConcatenationExpression>().operands();
    if (ops.size() == fields.size()) {
      bool per_elem = true;
      for (size_t i = 0; i < fields.size(); ++i) {
        if (ops[i]->type == nullptr || static_cast<int>(ops[i]->type->getBitWidth()) != fields[i].bits) {
          per_elem = false;
          break;
        }
      }
      if (per_elem) {
        note_write(sym, current_assign_nonblocking_, rhs.sourceRange.start());
        for (size_t i = 0; i < fields.size(); ++i) {
          const auto& f = fields[i];
          auto        v = fit_wrap(to_int_value(lower_rvalue(*ops[i])), f.bits, f.is_signed);
          put(f.name, v);
        }
        return true;
      }
    }
  }

  // `io = io2` whole copy from a sibling bundle struct: copy matching leaves.
  // Gate on the source ACTUALLY having leaves (struct_var_info_), not just on
  // the is_scalar_struct_var predicate: a whole-copied/deep-accessed source is
  // DECLARED flat (no leaves) even when the predicate holds at this later call
  // (CtrlBlock's `delayedWriteBack_1_bits_redirect =
  // delayedNotFlushedWriteBack_1_bits_redirect` emitted reads of `.bits`/
  // `.valid` leaves that were never declared -> "read of undefined variable").
  // A flat source falls through to the whole-value slice below, which reads it
  // per its real (flat) representation.
  if (r->kind == ExpressionKind::NamedValue) {
    const auto& osym = r->as<slang::ast::NamedValueExpression>().symbol;
    if (is_scalar_struct_var(osym)) {
      if (!declared_.contains(&osym)) {
        declare_value_symbol(osym, /*force_reg=*/false);
      }
      if (auto oit = struct_var_info_.find(&osym); oit != struct_var_info_.end()) {
        const bool o_is_tuple = oit->second.is_tuple;
        note_write(sym, current_assign_nonblocking_, rhs.sourceRange.start());
        for (const auto& f : fields) {
          // Read the source field per the SOURCE struct's representation.
          auto src = o_is_tuple ? read_struct_field_get(lname_of(osym), f.name)
                                : read_leaf(absl::StrCat(lname_of(osym), ".", f.name));
          put(f.name, src);
        }
        return true;
      }
    }
  }

  // Any other packed RHS (a function result, a ?: of structs, …): lower the whole
  // value once and slice each field out of it. The RHS does not read `io`'s own
  // fields, so this slice introduces no self-dependency.
  return assign_struct_whole_value(sym, to_int_value(lower_rvalue(rhs)), rhs.sourceRange.start());
}

bool Slang_context::assign_struct_whole_value(const slang::ast::ValueSymbol& sym, const std::string& value,
                                              slang::SourceLocation loc) {
  auto it = struct_var_info_.find(&sym);
  if (it == struct_var_info_.end()) {
    return false;
  }
  // Copy up front: builder calls below can insert into struct_var_info_ and
  // rehash it (same invalidation class as the assign_struct_whole snapshot).
  const bool is_tuple = it->second.is_tuple;
  const auto fields   = it->second.fields;
  auto       base     = lname_of(sym);
  auto       bi       = tinfo(sym.getType());
  auto       p        = to_pattern(to_int_value(value), bi.bits, false);
  note_write(sym, current_assign_nonblocking_, loc);
  for (const auto& f : fields) {
    auto fv = extract_field(p, f.off, f.bits);
    if (f.is_signed) {
      fv = builder_.create_sext_stmts(fv, std::to_string(f.bits - 1));
    }
    if (is_tuple) {
      emit_struct_field_set(base, f.name, fv);
    } else {
      emit_leaf_store(absl::StrCat(base, ".", f.name), fv);
    }
  }
  return true;
}

void Slang_context::assign_to_pattern(const slang::ast::Expression& lhs,
                                      std::span<const slang::ast::Expression* const> elems, const std::string& rhs) {
  const auto& ct = lhs.type->getCanonicalType();

  if (!ct.isIntegral() && ct.kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
    const auto&   arr   = ct.as<slang::ast::FixedSizeUnpackedArrayType>();
    const int     eb    = flat_or_tinfo(arr.elementType).bits;
    const int     flat  = eb * static_cast<int>(arr.range.width());
    const int64_t left  = arr.range.left;
    const int64_t lower = arr.range.lower();
    const int64_t step  = arr.range.right >= arr.range.left ? 1 : -1;
    auto          p     = to_pattern(to_int_value(rhs), flat, false);
    int64_t       k     = 0;
    for (const auto* e : elems) {
      const int64_t idx    = left + k * step;
      const int64_t lo_bit = (idx - lower) * eb;
      assign_to(pattern_lvalue_target(*e), extract_field(p, lo_bit, eb));
      ++k;
    }
    return;
  }

  // Packed target: MSB-first concatenation of the elements (element 0 highest).
  auto    ti     = tinfo(*lhs.type);
  auto    p      = to_pattern(to_int_value(rhs), ti.bits, false);
  int64_t offset = ti.bits;
  for (const auto* e : elems) {
    auto oi = tinfo(*e->type);
    offset -= oi.bits;
    assign_to(pattern_lvalue_target(*e), extract_field(p, offset < 0 ? 0 : offset, oi.bits));
  }
}

const Slang_context::Mem_info::Field* Slang_context::find_tuple_field(const Mem_info& mi, std::string_view name) const {
  for (const auto& f : mi.fields) {
    if (f.name == name) {
      return &f;
    }
  }
  return nullptr;
}

// store(ref 'mem', idx, const 'field', val): the PRE-detuple field-write shape
// (detuple rewrites it to the 3-child store('mem.field', idx, val)).
void Slang_context::emit_field_store(const std::string& mem_name, const std::string& idx, const std::string& field_name,
                                     const std::string& val) {
  auto& ln = *builder_.lnast;
  auto  st = builder_.add_child(Lnast_ntype::create_store());
  ln.add_child(st, Lnast_node::create_ref(mem_name));
  builder_.add_value_child_pub(st, idx);
  ln.add_child(st, Lnast_node::create_const(field_name));
  builder_.add_value_child_pub(st, val);
}

// tuple_get(t, mem, idx); tuple_get(d, t, const 'field'): the PRE-detuple
// field-read chain (detuple fuses it to tuple_get(d, 'mem.field', idx)).
// Returns the unsigned field-width temp `d`.
std::string Slang_context::emit_field_read_chain(const std::string& mem_name, const std::string& idx,
                                                 const std::string& field_name) {
  auto& ln  = *builder_.lnast;
  auto  tg1 = builder_.add_child(Lnast_ntype::create_tuple_get());
  auto  t1  = builder_.create_lnast_tmp();
  ln.add_child(tg1, Lnast_node::create_ref(t1));
  ln.add_child(tg1, Lnast_node::create_ref(mem_name));
  builder_.add_value_child_pub(tg1, idx);
  auto tg2 = builder_.add_child(Lnast_ntype::create_tuple_get());
  auto t2  = builder_.create_lnast_tmp();
  ln.add_child(tg2, Lnast_node::create_ref(t2));
  ln.add_child(tg2, Lnast_node::create_ref(t1));
  ln.add_child(tg2, Lnast_node::create_const(field_name));
  return t2;
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

  // Struct-element memory: reconstruct the packed element bus from per-field
  // reads (the inverse of the whole-element write decomposition). The index is
  // computed once and reused across the field reads.
  if (mi.is_tuple) {
    std::string acc;
    auto        mem_name = lname_of(*base_sym);
    for (const auto& f : mi.fields) {
      auto fv     = emit_field_read_chain(mem_name, idx, f.name);  // unsigned f.bits temp
      auto placed = to_pattern(fv, mi.elem_bits, false);
      if (f.off != 0) {
        placed = builder_.create_shl_stmts(placed, std::to_string(f.off));
      }
      acc = acc.empty() ? placed : builder_.create_bit_or_stmts({acc, placed});
    }
    return acc.empty() ? "0" : acc;
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

  // Struct-element memory whole-element write `mem[idx] <= rhs`: decompose into
  // one field store per field. `rhs` is the packed element value (whatever its
  // origin — struct literal, 'x, packed port bus, another element), so each
  // field's value is the matching bit-slice. detuple routes each to mem.field.
  if (mi.is_tuple) {
    note_write(*base_sym, current_assign_nonblocking_, lhs.sourceRange.start());
    auto p        = to_pattern(to_int_value(rhs), mi.elem_bits, false);
    auto mem_name = lname_of(*base_sym);
    for (const auto& f : mi.fields) {
      emit_field_store(mem_name, idx, f.name, extract_field(p, f.off, f.bits));
    }
    return;
  }

  auto val = to_pattern(to_int_value(rhs), mi.elem_bits, mi.elem_signed);

  note_write(*base_sym, current_assign_nonblocking_, lhs.sourceRange.start());

  auto& ln = *builder_.lnast;
  auto  st = builder_.add_child(Lnast_ntype::create_store());
  ln.add_child(st, Lnast_node::create_ref(lname_of(*base_sym)));
  builder_.add_value_child_pub(st, idx);
  builder_.add_value_child_pub(st, val);
}

// `mem[addr][const-chunk] <= data` — a chunked masked memory write (the XS SRAM
// byte/chunk write-enable idiom). A naive read-modify-write would be wrong here
// (a synchronous read gives last cycle's word, and multiple disjoint partial
// writes to one word would clobber each other). Instead lower it the way the
// hardware means it and the way the yosys-slang reference does ($memwr WR_EN):
// a memory write port whose enable is the per-chunk bit. The store carries the
// chunk index as an extra child; tolg sets the memory `wensize` (= #chunks) and
// the per-chunk enable. Disjoint chunks become independent write ports that the
// wensize memory wrapper merges. Only constant chunk-aligned slices are handled.
bool Slang_context::lower_mem_element_bitslice_write(const slang::ast::Expression& lhs, const std::string& rhs) {
  using slang::ast::RangeSelectionKind;

  // The slice base must be a single memory-element select: mem[addr].
  const auto& base = lhs.kind == ExpressionKind::ElementSelect ? lhs.as<slang::ast::ElementSelectExpression>().value()
                                                               : lhs.as<slang::ast::RangeSelectExpression>().value();
  if (base.kind != ExpressionKind::ElementSelect) {
    return false;
  }
  const auto& base_es = base.as<slang::ast::ElementSelectExpression>();
  const auto* mem_sym = resolve_base_symbol(base_es.value());
  if (mem_sym == nullptr || flat_port_syms_.contains(mem_sym)) {
    return false;  // flat-port arrays use the bit-slice path elsewhere
  }
  // The base must index a single memory element: an UNPACKED-array memory, OR a
  // memory-ized packed 2-D reg (register file, `logic [N:0][W:0][..]`). Both
  // store one word per index; this write targets a constant sub-chunk of that
  // word.  The read path (lower_rvalue) already routes packed_mem_regs_ here.
  if (!base_es.value().type->getCanonicalType().isUnpackedArray() && !packed_mem_regs_.contains(mem_sym)) {
    return false;
  }
  auto mit = mem_info_.find(mem_sym);
  if (mit == mem_info_.end()) {
    return false;
  }
  const auto& mi        = mit->second;
  const int   word_bits = mi.elem_bits;

  // The element word's type (e.g. `reg [3:0]`) gives the in-word bit indexing.
  const auto& elem_ty = base.type->getCanonicalType();
  if (!elem_ty.isIntegral() || !elem_ty.hasFixedRange()) {
    return false;
  }
  auto          range = elem_ty.getFixedRange();
  auto          ti    = tinfo(*lhs.type);
  const int64_t width = ti.bits;
  // A packed element word (`[3:0][1:0]`) indexes ELEMENTS, each spanning `stride`
  // bits; the index/range below is in element units and is scaled to a bit offset
  // after.  A flat word (`[31:0]`) has stride 1, so the register-file lowering is
  // unchanged.  Mirrors the read path (slang_expr.cpp lower_rvalue).
  const int     stride = elem_ty.isPackedArray() ? static_cast<int>(elem_ty.getArrayElementType()->getBitWidth()) : 1;

  // Constant low bit of the slice within the word; a dynamic in-word offset
  // returns false (the caller then diagnoses it as nested-lvalue).
  std::optional<int64_t> lo_bit;
  if (lhs.kind == ExpressionKind::ElementSelect) {
    if (auto ci = try_eval_int(lhs.as<slang::ast::ElementSelectExpression>().selector())) {
      lo_bit = range.isDescending() ? (*ci - range.lower()) : (range.upper() - *ci);
    }
  } else {
    const auto& rs   = lhs.as<slang::ast::RangeSelectExpression>();
    auto        kind = rs.getSelectionKind();
    if (kind == RangeSelectionKind::Simple) {
      auto l = try_eval_int(rs.left());
      auto r = try_eval_int(rs.right());
      if (l && r) {
        lo_bit = range.isDescending() ? std::min(*l, *r) - range.lower() : range.upper() - std::max(*l, *r);
      }
    } else if (auto b = try_eval_int(rs.left())) {
      if (kind == RangeSelectionKind::IndexedUp) {
        lo_bit = range.isDescending() ? (*b - range.lower()) : (range.upper() - *b - (width - 1));
      } else {  // IndexedDown
        lo_bit = range.isDescending() ? (*b - range.lower() - (width - 1)) : (range.upper() - *b);
      }
    }
  }
  if (lo_bit) {
    *lo_bit *= stride;  // element index -> bit offset within the word (stride 1 for a flat word)
  }
  if (!lo_bit || *lo_bit < 0 || width <= 0 || *lo_bit + width > word_bits) {
    return false;
  }
  // Uniform chunk granularity only (the SRAM WE model): the slice width must
  // divide the word and the offset must be chunk-aligned.
  if (word_bits % width != 0 || *lo_bit % width != 0) {
    return false;
  }
  const int64_t wensize = word_bits / width;  // number of write-enable chunks
  const int64_t chunk   = *lo_bit / width;    // which chunk this write targets

  // Position the chunk data within the full word (the other chunks are
  // don't-care — their write-enable bit is 0). din is the full element width.
  auto        val = to_pattern(rhs, static_cast<int>(width), ti.is_signed);
  std::string din = *lo_bit == 0 ? val : builder_.create_shl_stmts(val, std::to_string(*lo_bit));
  din             = to_pattern(to_int_value(din), word_bits, false);

  // Emit the per-memory wensize attr once (consumed by tolg's finalize_mems).
  if (wensize > 1 && !mem_wensize_emitted_.contains(mem_sym)) {
    mem_wensize_emitted_.insert(mem_sym);
    auto& ln   = *builder_.lnast;
    auto  aidx = builder_.add_child(Lnast_ntype::create_attr_set());
    ln.add_child(aidx, Lnast_node::create_ref(lname_of(*mem_sym)));
    ln.add_child(aidx, Lnast_node::create_const("wensize"));
    ln.add_child(aidx, Lnast_node::create_const(std::to_string(wensize)));
  }

  // Memory write port: store(mem, addr, din, chunk). The extra chunk child
  // (D+2 store children) marks a chunked write to tolg.
  auto idx = to_int_value(lower_rvalue(base_es.selector()));
  if (mi.lower != 0) {
    idx = builder_.create_minus_stmts(idx, std::to_string(mi.lower));
  }
  note_write(*mem_sym, current_assign_nonblocking_, lhs.sourceRange.start());
  auto& ln = *builder_.lnast;
  auto  st = builder_.add_child(Lnast_ntype::create_store());
  ln.add_child(st, Lnast_node::create_ref(lname_of(*mem_sym)));
  builder_.add_value_child_pub(st, idx);
  builder_.add_value_child_pub(st, din);
  builder_.add_value_child_pub(st, std::to_string(chunk));
  return true;
}

bool Slang_context::lower_mem_element_dynamic_write(const slang::ast::Expression& lhs, const std::string& rhs) {
  using slang::ast::RangeSelectionKind;

  const auto& base = lhs.kind == ExpressionKind::ElementSelect ? lhs.as<slang::ast::ElementSelectExpression>().value()
                                                               : lhs.as<slang::ast::RangeSelectExpression>().value();
  if (base.kind != ExpressionKind::ElementSelect) {
    return false;  // only `mem[addr][...]`
  }
  const auto& base_es = base.as<slang::ast::ElementSelectExpression>();
  if (!base_es.value().type->getCanonicalType().isUnpackedArray()) {
    return false;
  }
  const auto* mem_sym = resolve_base_symbol(base_es.value());
  if (mem_sym == nullptr || flat_port_syms_.contains(mem_sym)) {
    return false;  // flat-port arrays use the packed bit-slice path
  }
  auto mit = mem_info_.find(mem_sym);
  if (mit == mem_info_.end() || mit->second.is_tuple) {
    return false;  // struct (tuple) element field/bit writes handled elsewhere
  }
  const auto& mi      = mit->second;
  const auto& elem_ty = base.type->getCanonicalType();  // the element word type
  if (!elem_ty.isIntegral() || !elem_ty.hasFixedRange()) {
    return false;
  }
  auto      range = elem_ty.getFixedRange();
  auto      ti    = tinfo(*lhs.type);
  const int width = ti.bits;
  if (width <= 0) {
    return false;
  }

  // Dynamic in-word low-bit offset of the slice. Mirrors resolve_packed_lvalue's
  // normalize, but a memory-element base makes the packed path return false.
  auto offset_of = [&](const slang::ast::Expression& sel, int64_t wdown, int64_t wup) -> std::string {
    auto v = to_int_value(lower_rvalue(sel));
    if (range.isDescending()) {
      int64_t bias = range.lower() + (wdown - 1);
      return bias == 0 ? v : builder_.create_minus_stmts(v, std::to_string(bias));
    }
    int64_t bias = range.upper() - (wup - 1);
    return builder_.create_minus_stmts(std::to_string(bias), v);
  };
  std::string dyn_lo;
  if (lhs.kind == ExpressionKind::ElementSelect) {
    dyn_lo = offset_of(lhs.as<slang::ast::ElementSelectExpression>().selector(), 1, 1);
  } else {
    const auto& rs   = lhs.as<slang::ast::RangeSelectExpression>();
    auto        kind = rs.getSelectionKind();
    if (kind == RangeSelectionKind::IndexedUp) {
      dyn_lo = offset_of(rs.left(), 1, width);
    } else if (kind == RangeSelectionKind::IndexedDown) {
      dyn_lo = offset_of(rs.left(), width, 1);
    } else {
      return false;  // a Simple range with non-constant bounds: unsupported here
    }
  }

  // Memory index (biased), computed once for the read and the store.
  auto idx = to_int_value(lower_rvalue(base_es.selector()));
  if (mi.lower != 0) {
    idx = builder_.create_minus_stmts(idx, std::to_string(mi.lower));
  }

  // Read the addressed word (old contents, fwd=0), splice the new bits in at the
  // dynamic offset, write the whole word back.
  auto& ln  = *builder_.lnast;
  auto  tg  = builder_.add_child(Lnast_ntype::create_tuple_get());
  auto  cur = builder_.create_lnast_tmp();
  ln.add_child(tg, Lnast_node::create_ref(cur));
  ln.add_child(tg, Lnast_node::create_ref(lname_of(*mem_sym)));
  builder_.add_value_child_pub(tg, idx);

  auto cur_p    = to_pattern(cur, mi.elem_bits, false);
  auto val      = to_pattern(rhs, width, ti.is_signed);
  auto sel_mask = builder_.create_shl_stmts(mask_text(width), dyn_lo);
  auto keep     = builder_.create_bit_and_stmts(cur_p, builder_.create_bit_not_stmts(sel_mask));
  auto ins      = builder_.create_shl_stmts(val, dyn_lo);
  auto next     = builder_.create_bit_or_stmts({keep, ins});

  note_write(*mem_sym, current_assign_nonblocking_, lhs.sourceRange.start());
  auto st = builder_.add_child(Lnast_ntype::create_store());
  ln.add_child(st, Lnast_node::create_ref(lname_of(*mem_sym)));
  builder_.add_value_child_pub(st, idx);
  builder_.add_value_child_pub(st, next);
  return true;
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
  // Widen the shift amount so `+ bias` cannot drop its carry when cgen inlines it
  // into the Verilog shift operand (self-determined width = max(operand widths)).
  // Mirrors lower_select in slang_expr.cpp: a clog2(size)-bit idx makes `idx + 1`
  // wrap to 0 at the top index, silently returning the wrong element. Hold
  // flat_bits + bias, plus one headroom bit for the carry.
  int amt_bits = 0;
  for (int t = flat_bits + bias; t > 0; t >>= 1) {
    ++amt_bits;
  }
  ++amt_bits;
  shamt        = builder_.create_plus_stmts(trunc_to(shamt, amt_bits), std::to_string(bias));
  auto shifted = builder_.create_sra_stmts(builder_.create_shl_stmts(p, std::to_string(bias)), shamt);
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
  // NamedValue and HierarchicalValue are both ValueExpressionBase with a resolved
  // `.symbol`.  Hierarchical = a reference into a (const-folded) named generate
  // block, e.g. an unpacked-array read `gen[g-1].s[2*i+0]` (PriorityEncoder): the
  // base symbol is the genblock instance's array, declared on demand here.
  if (base.kind == ExpressionKind::NamedValue || base.kind == ExpressionKind::HierarchicalValue) {
    const auto& sym = base.as<slang::ast::ValueExpressionBase>().symbol;
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

bool Slang_context::resolve_packed_lvalue(const slang::ast::Expression& lhs, Packed_lv& out) {
  using slang::ast::RangeSelectionKind;

  switch (lhs.kind) {
    case ExpressionKind::NamedValue: {
      const auto& sym = lhs.as<slang::ast::NamedValueExpression>().symbol;
      const auto& ct  = sym.getType().getCanonicalType();
      if (!ct.isIntegral() || !ct.hasFixedRange()) {
        return false;  // unpacked / non-packed root: not a packed slice
      }
      if (!declared_.contains(&sym) && !input_syms_.contains(&sym)) {
        declare_value_symbol(sym, /*force_reg=*/false);
      }
      auto ti       = tinfo(sym.getType());
      out.base      = &sym;
      out.const_off = 0;
      out.dyn_off.clear();
      out.width     = ti.bits;
      out.is_signed = ti.is_signed;
      return true;
    }

    case ExpressionKind::Conversion:
      // a same-bitstream-width bitcast passes through to the inner target
      return resolve_packed_lvalue(lhs.as<slang::ast::ConversionExpression>().operand(), out);

    case ExpressionKind::MemberAccess: {
      const auto& ma = lhs.as<slang::ast::MemberAccessExpression>();
      if (ma.member.kind != slang::ast::SymbolKind::Field || !ma.value().type->isIntegral()) {
        return false;
      }
      if (!resolve_packed_lvalue(ma.value(), out)) {
        return false;
      }
      const auto& field = ma.member.as<slang::ast::FieldSymbol>();
      auto        ti    = tinfo(*lhs.type);
      out.const_off += static_cast<int64_t>(field.bitOffset);  // field offset within its struct
      out.width     = ti.bits;
      out.is_signed = ti.is_signed;
      return true;
    }

    case ExpressionKind::ElementSelect:
    case ExpressionKind::RangeSelect: {
      const auto& base = lhs.kind == ExpressionKind::ElementSelect ? lhs.as<slang::ast::ElementSelectExpression>().value()
                                                                   : lhs.as<slang::ast::RangeSelectExpression>().value();
      const auto& base_ty = base.type->getCanonicalType();

      // Flattened array element: `arr[idx]` is a bit-slice of the packed bus
      // var. The element-select base is the unpacked array, so the generic
      // packed recursion (which needs a packed base) does not apply — resolve
      // it directly to the bus root plus the element bit-offset. A `.field` /
      // `[slice]` wrapper above then folds in via the MemberAccess/packed cases.
      if (lhs.kind == ExpressionKind::ElementSelect && base_ty.isUnpackedArray()) {
        const auto* fsym = resolve_base_symbol(base);
        if (fsym != nullptr && flat_port_syms_.contains(fsym)) {
          const auto& mi = mem_info_.at(fsym);
          out.base       = fsym;
          out.const_off  = 0;
          out.dyn_off.clear();
          out.width     = mi.elem_bits;
          out.is_signed = mi.elem_signed;
          const auto& sel = lhs.as<slang::ast::ElementSelectExpression>().selector();
          if (auto ci = try_eval_int(sel)) {
            out.const_off = (*ci - mi.lower) * mi.elem_bits;
          } else {
            auto idx = to_int_value(lower_rvalue(sel));
            if (mi.lower != 0) {
              idx = builder_.create_minus_stmts(idx, std::to_string(mi.lower));
            }
            out.dyn_off = mi.elem_bits != 1 ? builder_.create_mult_stmts(idx, std::to_string(mi.elem_bits)) : idx;
          }
          return true;
        }
      }

      if (!base_ty.isIntegral() || !base_ty.hasFixedRange()) {
        return false;  // unpacked array element / non-packed base
      }
      if (!resolve_packed_lvalue(base, out)) {
        return false;
      }

      auto    range  = base_ty.getFixedRange();
      int     stride = base_ty.isPackedArray() ? static_cast<int>(base_ty.getArrayElementType()->getBitWidth()) : 1;
      auto    ti     = tinfo(*lhs.type);

      // local low-bit offset within `base`, in element units (const and/or dynamic)
      std::optional<int64_t> const_low;
      std::string            dyn_low;
      auto normalize = [&](const slang::ast::Expression& idx, int64_t width_down,
                           int64_t width_up) -> std::pair<std::optional<int64_t>, std::string> {
        if (auto ci = try_eval_int(idx)) {
          int64_t bottom = range.isDescending() ? (*ci - range.lower() - (width_down - 1))
                                                : (range.upper() - *ci - (width_up - 1));
          return {bottom, {}};
        }
        auto v = to_int_value(lower_rvalue(idx));  // settle the selector to an int (match the rvalue select path)
        if (range.isDescending()) {
          int64_t bias = range.lower() + (width_down - 1);
          return {std::nullopt, bias == 0 ? v : builder_.create_minus_stmts(v, std::to_string(bias))};
        }
        int64_t bias = range.upper() - (width_up - 1);
        return {std::nullopt, builder_.create_minus_stmts(std::to_string(bias), v)};
      };

      if (lhs.kind == ExpressionKind::ElementSelect) {
        std::tie(const_low, dyn_low) = normalize(lhs.as<slang::ast::ElementSelectExpression>().selector(), 1, 1);
      } else {
        const auto& rs   = lhs.as<slang::ast::RangeSelectExpression>();
        auto        kind = rs.getSelectionKind();
        if (kind == RangeSelectionKind::Simple) {
          auto l = try_eval_int(rs.left());
          auto r = try_eval_int(rs.right());
          if (!l || !r) {
            return false;  // non-const simple range bounds: unsupported here
          }
          const_low = range.isDescending() ? std::min(*l, *r) - range.lower() : range.upper() - std::max(*l, *r);
        } else {
          int64_t w = ti.bits / stride;
          if (kind == RangeSelectionKind::IndexedUp) {
            std::tie(const_low, dyn_low) = normalize(rs.left(), 1, w);
          } else {
            std::tie(const_low, dyn_low) = normalize(rs.left(), w, 1);
          }
        }
      }

      // fold the local offset (bits) into the accumulator
      if (const_low) {
        out.const_off += (*const_low) * stride;
      }
      if (!dyn_low.empty()) {
        std::string term = stride == 1 ? dyn_low : builder_.create_mult_stmts(dyn_low, std::to_string(stride));
        out.dyn_off      = out.dyn_off.empty() ? term : builder_.create_plus_stmts(out.dyn_off, term);
      }
      out.width     = ti.bits;
      out.is_signed = ti.is_signed;
      return true;
    }

    default: return false;
  }
}

void Slang_context::emit_packed_rmw(const Packed_lv& lv, const std::string& rhs, slang::SourceRange sr) {
  // value written into the slice comes from the LOW bits of the RHS
  auto val       = to_pattern(rhs, static_cast<int>(lv.width), lv.is_signed);
  auto base_name = lname_of(*lv.base);

  if (lv.dyn_off.empty()) {
    int64_t lo_bit = lv.const_off;
    if (lo_bit < 0) {
      emit_warning(sr, "select-out-of-range", "bitwidth", "constant select is out of the declared range");
      lo_bit = 0;
    }
    std::string sel_mask = lo_bit == 0
                               ? mask_text(static_cast<int>(lv.width))
                               : std::string(Dlop::get_mask_value(lo_bit + lv.width - 1, lo_bit)->to_pyrope());
    note_write(*lv.base, current_assign_nonblocking_, sr.start());
    builder_.create_set_mask_stmts(base_name, sel_mask, val);
    return;
  }

  // Dynamic offset: tolg requires const set_mask masks, so lower an explicit
  // read-modify-write with shifts (and/or/shl) on the full base.
  auto bi    = tinfo(lv.base->getType());
  auto cur   = read_symbol(*lv.base, sr);
  auto cur_p = to_pattern(cur, bi.bits, bi.is_signed);

  std::string shamt = lv.const_off == 0 ? lv.dyn_off : builder_.create_plus_stmts(lv.dyn_off, std::to_string(lv.const_off));
  auto        sel_mask = builder_.create_shl_stmts(mask_text(static_cast<int>(lv.width)), shamt);
  auto        keep     = builder_.create_bit_and_stmts(cur_p, builder_.create_bit_not_stmts(sel_mask));
  auto        ins      = builder_.create_shl_stmts(val, shamt);
  auto        next     = builder_.create_bit_or_stmts({keep, ins});
  if (bi.is_signed) {
    next = builder_.create_sext_stmts(next, std::to_string(bi.bits - 1));
  }
  note_write(*lv.base, current_assign_nonblocking_, sr.start());
  builder_.create_assign_stmts(base_name, next);
}
