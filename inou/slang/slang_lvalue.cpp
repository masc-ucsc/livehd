//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Assignment-target lowering (todo/ 2s subtask B). Targets decompose the
// yosys-slang LValue::analyze way - variable, element/range select (through
// the same base+offset math as the rvalue side, onto set_mask), packed
// member access, concatenation (MSB-first split of the RHS), memory element
// write - instead of a fixed three-case switch.

#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/types/AllTypes.h"
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

      // An element/range select on an unpacked array is a memory/flat-port
      // write (single-element only); handled separately.
      if (base_ty.isUnpackedArray()) {
        lower_unpacked_write(lhs, rhs);
        return;
      }

      // `mem[addr][const-chunk] <= data`: a chunked masked memory write (the
      // base is a packed element of an unpacked array). Lowered via the memory
      // write-enable-granularity (wensize) path before the packed-root attempt.
      if (lower_mem_element_bitslice_write(lhs, rhs)) {
        return;
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
  if (!base_es.value().type->getCanonicalType().isUnpackedArray()) {
    return false;
  }
  const auto* mem_sym = resolve_base_symbol(base_es.value());
  if (mem_sym == nullptr || flat_port_syms_.contains(mem_sym)) {
    return false;  // flat-port arrays use the bit-slice path elsewhere
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
