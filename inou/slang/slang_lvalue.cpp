//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Assignment-target lowering (todo/ 2s subtask B). Targets decompose the
// yosys-slang LValue::analyze way - variable, element/range select (through
// the same base+offset math as the rvalue side, onto set_mask), packed
// member access, concatenation (MSB-first split of the RHS), memory element
// write - instead of a fixed three-case switch.

#include <algorithm>
#include <bit>

#include "absl/strings/str_cat.h"
#include "slang/ast/ASTVisitor.h"
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

// Collect top-level fields read from one aggregate (`target.field`, including
// deeper paths such as `target.field.sub`).  A continuous assignment-pattern
// is one simultaneous packed assignment in SystemVerilog, but the bundle
// lowering below emits one leaf store at a time.  Therefore a field whose RHS
// reads a sibling must be emitted after that sibling's driver; otherwise the
// early read of a `mut` leaf resolves to nil before the later store exists.
struct Self_field_read_collector : public slang::ast::ASTVisitor<Self_field_read_collector, slang::ast::VisitFlags::AllGood> {
  const slang::ast::ValueSymbol*    target = nullptr;
  absl::flat_hash_set<std::string>* reads  = nullptr;

  void handle(const slang::ast::MemberAccessExpression& ma) {
    const slang::ast::Expression* cur = &ma;
    std::vector<std::string_view> path;
    while (cur->kind == ExpressionKind::MemberAccess) {
      const auto& m = cur->as<slang::ast::MemberAccessExpression>();
      path.push_back(m.member.name);
      cur = &m.value();
    }
    while (cur->kind == ExpressionKind::Conversion) {
      cur = &cur->as<slang::ast::ConversionExpression>().operand();
    }
    if (!path.empty() && (cur->kind == ExpressionKind::NamedValue || cur->kind == ExpressionKind::HierarchicalValue)
        && &cur->as<slang::ast::ValueExpressionBase>().symbol == target) {
      // The member chain is collected outside-in, so the last item is the
      // top-level field represented by Struct_info::fields.
      reads->insert(std::string(path.back()));
    }
    visitDefault(ma);
  }
};

// `if in_range { ... }` around every statement emitted while it lives; nothing
// when `in_range` is empty (an address that is always in range).
class If_in_range {
public:
  If_in_range(Lnast_builder& b, const std::string& in_range) : b_(b), open_(!in_range.empty()) {
    if (open_) {
      auto guard = b_.create_if_stmt(false);
      b_.add_if_cond(guard, in_range);
      b_.push_stmts(b_.add_if_stmts(guard));
    }
  }
  ~If_in_range() {
    if (open_) {
      b_.pop_stmts();
    }
  }
  If_in_range(const If_in_range&)            = delete;
  If_in_range& operator=(const If_in_range&) = delete;

private:
  Lnast_builder& b_;
  bool           open_;
};

// `bounds` of `v` mapped through `bias + sign * v` (sign = +1 or -1).
std::optional<std::pair<int64_t, int64_t>> affine_bounds(const std::optional<std::pair<int64_t, int64_t>>& bounds, int64_t bias,
                                                         int sign) {
  if (!bounds) {
    return std::nullopt;
  }
  return sign > 0 ? std::pair{bias + bounds->first, bias + bounds->second} : std::pair{bias - bounds->second, bias - bounds->first};
}

// `bounds` scaled by a positive `stride` (nullopt on overflow).
std::optional<std::pair<int64_t, int64_t>> scaled_bounds(const std::optional<std::pair<int64_t, int64_t>>& bounds, int64_t stride) {
  std::pair<int64_t, int64_t> out;
  if (!bounds || __builtin_mul_overflow(bounds->first, stride, &out.first)
      || __builtin_mul_overflow(bounds->second, stride, &out.second)) {
    return std::nullopt;
  }
  return out;
}
}  // namespace

// A Verilog index is often a 32-bit expression over a narrow variable (`k*8`,
// `{k, 3'b000}`, `b - 1`), so its type alone puts the position far past what
// it selects on either side. Every subexpression is computed exactly, then
// kept only when it fits its own type (a result that may wrap takes the whole
// type range).
std::optional<std::pair<int64_t, int64_t>> Slang_context::selector_bounds(const slang::ast::Expression& e) {
  using slang::ast::BinaryOperator;
  using Bounds    = std::pair<__int128, __int128>;
  const auto bits = static_cast<int64_t>(e.type->getBitWidth());
  if (!e.type->isIntegral() || bits <= 0 || bits >= 62) {
    return std::nullopt;
  }
  const Bounds full = e.type->isSigned() ? Bounds{-(__int128{1} << (bits - 1)), (__int128{1} << (bits - 1)) - 1}
                                         : Bounds{0, (__int128{1} << bits) - 1};
  const auto   sub  = [&](const slang::ast::Expression& x) -> std::optional<Bounds> {
    if (auto b = selector_bounds(x)) {
      return Bounds{b->first, b->second};
    }
    return std::nullopt;
  };
  const auto exact = [&]() -> std::optional<Bounds> {
    if (auto c = try_eval_int(e)) {
      return Bounds{*c, *c};
    }
    switch (e.kind) {
      case ExpressionKind::Conversion   : return sub(e.as<slang::ast::ConversionExpression>().operand());
      case ExpressionKind::ConditionalOp: {
        const auto& ce = e.as<slang::ast::ConditionalExpression>();
        const auto  l  = sub(ce.left());
        const auto  r  = sub(ce.right());
        if (!l || !r) {
          return std::nullopt;
        }
        return Bounds{std::min(l->first, r->first), std::max(l->second, r->second)};
      }
      case ExpressionKind::Concatenation: {
        // MSB operand first; each lands as its bit pattern.
        Bounds acc{0, 0};
        for (const auto* op : e.as<slang::ast::ConcatenationExpression>().operands()) {
          const auto w = static_cast<int64_t>(op->type->getBitWidth());
          auto       b = sub(*op);
          if (!b || b->first < 0) {
            b = Bounds{0, (__int128{1} << w) - 1};
          }
          acc = {(acc.first << w) | b->first, (acc.second << w) | b->second};
        }
        return acc;
      }
      case ExpressionKind::BinaryOp: {
        const auto& bo = e.as<slang::ast::BinaryExpression>();
        const auto  l  = sub(bo.left());
        const auto  r  = sub(bo.right());
        if (!l || !r) {
          return std::nullopt;
        }
        const bool const_amount = r->first == r->second && r->first >= 0 && r->first < 62;
        switch (bo.op) {
          case BinaryOperator::Add     : return Bounds{l->first + r->first, l->second + r->second};
          case BinaryOperator::Subtract: return Bounds{l->first - r->second, l->second - r->first};
          case BinaryOperator::Multiply: {
            const auto p = {l->first * r->first, l->first * r->second, l->second * r->first, l->second * r->second};
            return Bounds{std::min(p), std::max(p)};
          }
          case BinaryOperator::LogicalShiftLeft:
          case BinaryOperator::ArithmeticShiftLeft:
            if (!const_amount) {
              return std::nullopt;
            }
            return Bounds{l->first * (__int128{1} << r->first), l->second * (__int128{1} << r->first)};
          case BinaryOperator::LogicalShiftRight:
          case BinaryOperator::ArithmeticShiftRight:
            if (!const_amount || l->first < 0) {
              return std::nullopt;
            }
            return Bounds{l->first >> r->first, l->second >> r->first};
          default: return std::nullopt;
        }
      }
      default: return std::nullopt;
    }
  }();
  const auto out = exact && exact->first >= full.first && exact->second <= full.second ? *exact : full;
  return std::pair{static_cast<int64_t>(out.first), static_cast<int64_t>(out.second)};
}

void Slang_context::note_write(const slang::ast::Symbol& sym, bool nonblocking, slang::SourceLocation loc) {
  if (proc_kind_ == Proc_kind::none) {
    return;
  }
  auto [it, inserted] = proc_assign_style_.try_emplace(&sym, nonblocking);
  if (!inserted && it->second != nonblocking) {
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
    emit_warning(expr.timingControl->sourceRange,
                 "intra-assign-timing",
                 "unsupported",
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
    if (l->kind == ExpressionKind::NamedValue || l->kind == ExpressionKind::HierarchicalValue) {
      const auto& lsym = l->as<slang::ast::ValueExpressionBase>().symbol;
      if (is_scalar_struct_var(lsym)) {
        current_assign_nonblocking_ = expr.isNonBlocking();
        if (assign_struct_whole(lsym, expr.right())) {
          return;
        }
      }
    }
  }

  // `array <= '{default: 0}` has aggregate type, so it cannot be represented
  // by lower_rvalue's scalar lname/literal return value. In Pyrope/LNAST this
  // is the array broadcast spelling `array = 0`; lower it directly to the
  // covered element stores (and per-field stores for struct-element arrays).
  if (!expr.isCompound() && is_zero_fill_pattern(expr.right())) {
    current_assign_nonblocking_ = expr.isNonBlocking();
    if (lower_unpacked_zero_fill(lhs)) {
      return;
    }
  }

  if (!expr.isCompound()) {
    current_assign_nonblocking_ = expr.isNonBlocking();
    if (lower_unpacked_whole_copy(lhs, expr.right())) {
      return;
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

bool Slang_context::is_zero_fill_pattern(const slang::ast::Expression& raw) {
  const slang::ast::Expression* expr = &raw;
  while (expr->kind == ExpressionKind::Conversion) {
    expr = &expr->as<slang::ast::ConversionExpression>().operand();
  }

  std::span<const slang::ast::Expression* const> elems;
  switch (expr->kind) {
    case ExpressionKind::SimpleAssignmentPattern:
      elems = expr->as<slang::ast::SimpleAssignmentPatternExpression>().elements();
      break;
    case ExpressionKind::StructuredAssignmentPattern:
      elems = expr->as<slang::ast::StructuredAssignmentPatternExpression>().elements();
      break;
    case ExpressionKind::ReplicatedAssignmentPattern:
      elems = expr->as<slang::ast::ReplicatedAssignmentPatternExpression>().elements();
      break;
    default: return false;  // plain scalar zero already follows the normal path
  }
  if (elems.empty()) {
    return false;
  }
  for (const auto* elem : elems) {
    if (is_zero_fill_pattern(*elem)) {
      continue;
    }
    auto cv = try_eval_const_net(*elem);
    if (!cv || !cv->isInteger()) {
      return false;
    }
    auto v = Dlop::from_pyrope(const_text(cv->integer()));
    if (!v || !v->is_known_zero()) {
      return false;
    }
  }
  return true;
}

bool Slang_context::lower_unpacked_zero_fill(const slang::ast::Expression& raw_lhs) {
  const slang::ast::Expression* lhs = &raw_lhs;
  while (lhs->kind == ExpressionKind::Conversion) {
    lhs = &lhs->as<slang::ast::ConversionExpression>().operand();
  }

  std::vector<const slang::ast::Expression*> sels;
  const slang::ast::Expression*              base  = lhs;
  bool                                       whole = false;
  if (lhs->kind == ExpressionKind::ElementSelect) {
    base = peel_unpacked_chain(*lhs, sels);
  } else if (lhs->kind == ExpressionKind::NamedValue || lhs->kind == ExpressionKind::HierarchicalValue) {
    whole = true;
  } else if (lhs->kind == ExpressionKind::RangeSelect) {
    // CVA6's `events[MHPMCounterNum:1] = '{default: 0}` selects the complete
    // one-dimensional unpacked array. Accept only a full-range slice here;
    // partial aggregate slices need their own index-order lowering.
    const auto& rs = lhs->as<slang::ast::RangeSelectExpression>();
    base           = &rs.value();
    const auto* bs = resolve_base_symbol(*base);
    auto        mi = bs == nullptr ? mem_info_.end() : mem_info_.find(bs);
    auto        l  = try_eval_int(rs.left());
    auto        r  = try_eval_int(rs.right());
    if (mi == mem_info_.end() || mi->second.rank() != 1 || !l || !r) {
      return false;
    }
    const int64_t lo = mi->second.lower;
    const int64_t hi = lo + mi->second.size - 1;
    if (std::min(*l, *r) != lo || std::max(*l, *r) != hi) {
      return false;
    }
    whole = true;
  } else {
    return false;
  }

  const auto* base_sym = resolve_base_symbol(*base);
  auto        mit      = base_sym == nullptr ? mem_info_.end() : mem_info_.find(base_sym);
  if (mit == mem_info_.end()) {
    return false;
  }
  const auto mi   = mit->second;  // builder calls below may rehash mem_info_
  const auto rank = mi.rank();
  if ((!whole && sels.empty()) || sels.size() > rank) {
    return false;
  }

  note_write(*base_sym, current_assign_nonblocking_, lhs->sourceRange.start());

  // A const-indexed single-dimensional array is represented as one packed bus.
  // A full zero fill is therefore exactly one store, preserving the aggregate
  // identity instead of manufacturing one set_mask per element.
  if (flat_port_syms_.contains(base_sym) && whole) {
    builder_.create_assign_stmts(write_target_of(*base_sym), "0");
    return true;
  }
  if (flat_port_syms_.contains(base_sym)) {
    return false;
  }

  int64_t suffix_count = 1;
  if (!whole) {
    const auto& dims = mi.dims;
    for (size_t k = sels.size(); k < rank; ++k) {
      suffix_count *= dims.empty() ? mi.size : dims[k].width;
    }
  } else {
    suffix_count = mi.size;
  }
  if (suffix_count <= 0 || suffix_count > options_.unroll_limit) {
    emit_error(
        lhs->sourceRange,
        "unroll-limit",
        "comptime",
        std::format("zero-fill expansion needs {} element stores, exceeding the limit of {}", suffix_count, options_.unroll_limit));
    return true;
  }

  Unpacked_address addr;
  addr.index = "0";
  if (!whole) {
    addr = build_unpacked_address(mi, sels);
    if (suffix_count != 1) {
      addr.index = builder_.create_mult_stmts(addr.index, std::to_string(suffix_count));
    }
  }

  const auto name = write_target_of(*base_sym);
  auto&      ln   = *builder_.lnast;
  emit_if_in_range(addr, [&]() {
    for (int64_t off = 0; off < suffix_count; ++off) {
      std::string idx = addr.index;
      if (off != 0) {
        idx = addr.index == "0" ? std::to_string(off) : builder_.create_plus_stmts(addr.index, std::to_string(off));
      }
      if (mi.is_tuple) {
        for (const auto& field : mi.fields) {
          emit_field_store(name, idx, field.name, "0");
        }
      } else {
        auto st = builder_.add_child(Lnast_ntype::create_store());
        ln.add_child(st, Lnast_node::create_ref(name));
        builder_.add_value_child_pub(st, idx);
        builder_.add_value_child_pub(st, "0");
      }
    }
  });
  return true;
}

bool Slang_context::lower_unpacked_whole_copy(const slang::ast::Expression& raw_lhs, const slang::ast::Expression& raw_rhs) {
  const slang::ast::Expression* lhs = &raw_lhs;
  const slang::ast::Expression* rhs = &raw_rhs;
  while (lhs->kind == ExpressionKind::Conversion) {
    lhs = &lhs->as<slang::ast::ConversionExpression>().operand();
  }
  while (rhs->kind == ExpressionKind::Conversion) {
    rhs = &rhs->as<slang::ast::ConversionExpression>().operand();
  }
  const bool lhs_named = lhs->kind == ExpressionKind::NamedValue || lhs->kind == ExpressionKind::HierarchicalValue;
  const bool rhs_named = rhs->kind == ExpressionKind::NamedValue || rhs->kind == ExpressionKind::HierarchicalValue;
  if (!lhs_named || !rhs_named) {
    return false;
  }
  const auto& lsym = lhs->as<slang::ast::ValueExpressionBase>().symbol;
  const auto& rsym = rhs->as<slang::ast::ValueExpressionBase>().symbol;

  // Combinational locals are declared lazily at their first access, while
  // state arrays are hoisted into the module prologue. Make both operands'
  // split-memory metadata visible before deciding whether this first access is
  // an aggregate copy; assign_to/lower_rvalue would otherwise declare them
  // only after this special case has already fallen through.
  //
  // AGGREGATES ONLY. This function is a PREDICATE for the caller: it returns
  // false for every plain scalar `a = b`, and a declare forced here would then
  // fire on the false path too -- minting a `mut` + `0sb?` poison driver for a
  // scalar the caller has not decided anything about yet, at a position that may
  // be inside an if/case arm. `mem_info_` is only ever populated for a symbol
  // that already carries the array/flat-bus machinery, so gate on exactly that.
  const auto ensure_aggregate_decl = [&](const slang::ast::ValueSymbol& sym) {
    if (declared_.contains(&sym) || input_syms_.contains(&sym)) {
      return;
    }
    if (!mem_syms_.contains(&sym) && !sym.getType().getCanonicalType().isUnpackedArray()) {
      return;
    }
    declare_value_symbol(sym, /*force_reg=*/false);
  };
  ensure_aggregate_decl(lsym);
  ensure_aggregate_decl(rsym);
  auto lit = mem_info_.find(&lsym);
  auto rit = mem_info_.find(&rsym);
  if (lit == mem_info_.end() || rit == mem_info_.end()) {
    return false;
  }
  const auto lmi = lit->second;
  const auto rmi = rit->second;
  if (lmi.size != rmi.size || lmi.elem_bits != rmi.elem_bits) {
    return false;
  }
  if (lmi.is_tuple && rmi.is_tuple) {
    if (lmi.fields.size() != rmi.fields.size()) {
      return false;
    }
    for (size_t k = 0; k < lmi.fields.size(); ++k) {
      const auto& lf = lmi.fields[k];
      const auto& rf = rmi.fields[k];
      if (lf.name != rf.name || lf.off != rf.off || lf.bits != rf.bits || lf.is_signed != rf.is_signed) {
        return false;
      }
    }
  } else if (lmi.is_tuple) {
    if (!flat_port_syms_.contains(&rsym)) {
      return false;
    }
  } else if (rmi.is_tuple) {
    if (!flat_port_syms_.contains(&lsym) || rmi.rank() != 1 || rmi.size > 64) {
      return false;
    }
  } else if (lmi.rank() != 1 || rmi.rank() != 1) {
    return false;
  }
  if (&lsym == &rsym) {
    return true;  // aggregate hold
  }

  note_write(lsym, current_assign_nonblocking_, lhs->sourceRange.start());
  const auto lname = write_target_of(lsym);
  const auto rname = lname_of(rsym);

  // A plain array may be represented either as a Memory (entry zero in the
  // packed read_all/update bus LSB) or as a flat packed bus (the declaration's
  // rightmost element in the LSB, matching SystemVerilog port/aggregate
  // layout). Those orders differ for an ascending range. Whole-array copies
  // are positional (leftmost-to-leftmost), so reverse element lanes exactly
  // when the two internal representations disagree. Without this bridge,
  // `logic [W-1:0] d [N]; d = q; d[i] = x; q <= d;` updates N-1-i after a
  // Slang -> Pyrope round trip.
  if (!lmi.is_tuple) {
    const int  total_bits   = static_cast<int>(lmi.size * lmi.elem_bits);
    const bool lhs_flat     = flat_port_syms_.contains(&lsym);
    const bool rhs_flat     = flat_port_syms_.contains(&rsym);
    const bool lhs_reversed = !lhs_flat && !lmi.descending;
    const bool rhs_reversed = !rhs_flat && !rmi.descending;
    auto       packed_rhs   = to_pattern(read_symbol(rsym, rhs->sourceRange), total_bits, false);

    if (lhs_reversed != rhs_reversed) {
      std::string reordered;
      for (int64_t dst = 0; dst < lmi.size; ++dst) {
        const int64_t src  = lmi.size - 1 - dst;
        auto          lane = extract_field(packed_rhs, src * lmi.elem_bits, lmi.elem_bits);
        if (dst != 0) {
          lane = builder_.create_shl_stmts(lane, std::to_string(dst * lmi.elem_bits));
        }
        reordered = reordered.empty() ? lane : builder_.create_bit_or_stmts({reordered, lane});
      }
      packed_rhs = std::move(reordered);
    }
    builder_.create_assign_stmts(lname, packed_rhs);
    return true;
  }

  // A struct-element memory has no live aggregate backing value after
  // detuple: its storage is the set of scalar field arrays (`mem.field`). A
  // whole assignment between two such memories must therefore be split along
  // the same boundary as their declarations. Keep each field assignment whole
  // instead of expanding it to one store per entry: a mut field gets one
  // packed combinational replacement, and a reg field gets one bulk memory
  // update. Emit the dotted refs directly because detuple creates declarations
  // with exactly these names and otherwise leaves already-split operations
  // untouched.
  if (rmi.is_tuple) {
    auto& ln = *builder_.lnast;
    for (const auto& field : lmi.fields) {
      auto st = builder_.add_child(Lnast_ntype::create_store());
      ln.add_child(st, Lnast_node::create_ref(absl::StrCat(lname, ".", field.name)));
      ln.add_child(st, Lnast_node::create_ref(absl::StrCat(rname, ".", field.name)));
    }
    return true;
  }

  // A flat packed array has no per-field arrays to copy. Retain the existing
  // lane extraction for a stateful tuple-memory destination; a combinational
  // flat destination follows the ordinary packed assignment path.
  if (!reg_syms_.contains(&lsym)) {
    return false;
  }
  std::string packed_rhs;
  packed_rhs = to_pattern(read_symbol(rsym, rhs->sourceRange), static_cast<int>(lmi.size * lmi.elem_bits), false);
  for (int64_t idx = 0; idx < lmi.size; ++idx) {
    const auto index = std::to_string(idx);
    for (const auto& field : lmi.fields) {
      const auto lane  = lmi.descending ? idx : lmi.size - 1 - idx;
      auto       value = extract_field(packed_rhs, lane * lmi.elem_bits + field.off, field.bits);
      emit_field_store(lname, index, field.name, value);
    }
  }
  return true;
}

void Slang_context::assign_to(const slang::ast::Expression& lhs, const std::string& rhs) {
  switch (lhs.kind) {
    // A HierarchicalValue target (`gen_blk.sig = v`, e.g. an output-port
    // connection driving a named generate block's net) is a ValueExpressionBase
    // with the symbol already resolved — same lowering as NamedValue (the
    // rvalue side already reads both alike).
    case ExpressionKind::NamedValue       :
    case ExpressionKind::HierarchicalValue: {
      const auto& nv  = lhs.as<slang::ast::ValueExpressionBase>();
      const auto& sym = nv.symbol;
      if (!declared_.contains(&sym) && !input_syms_.contains(&sym)) {
        declare_value_symbol(sym, /*force_reg=*/false);
      }
      auto name  = lname_of(sym);
      // A partially-registered var writes its FLOP from an edge process while
      // reads (including `rhs`) resolve to the combinational composite, so the
      // two names differ and the hold shortcut below must not fire on them.
      auto wname = write_target_of(sym);
      if (rhs == name && wname == name) {
        return;  // full-width self-assign (`q <= q;` hold idiom): an unwritten reg already holds
      }
      if (bit_regs_.contains(&sym) || packed_wire_bits_.contains(&sym)) {
        Packed_lv lv;
        if (resolve_packed_lvalue(lhs, lv)) {
          emit_packed_rmw(lv, rhs, lhs.sourceRange);
        } else {
          // Never fall through to the flat store: that would drive the READ-ONLY
          // packed view and silently lose the write to every bit flop.
          emit_unsupported(lhs.sourceRange,
                           "unsupported-bit-clock-write",
                           "independently clocked vector bits require a packed destination");
        }
        return;
      }
      // A bundle-declared struct var has NO flat net — every read resolves
      // through its leaves, so a flat store would be dead (undriven leaves,
      // dropped instance-output bindings: the _vsetModule_io_out/ExeUnitImp_4
      // family). Split the already-lowered value onto the leaves instead.
      if (assign_struct_whole_value(sym, rhs, lhs.sourceRange.start())) {
        return;
      }
      // M7: a WHOLE write to a BUNDLE output port (`resp = expr;`, `resp = '0;`)
      // splits the flat value into per-field stores — the tuple port has no
      // flat net either.
      if (assign_bundle_port_whole_value(sym, rhs, lhs.sourceRange.start())) {
        return;
      }
      note_write(sym, current_assign_nonblocking_, lhs.sourceRange.start());
      if (packed_mem_regs_.contains(&sym)) {
        // A packed vector kept as an indexed memory still assigns BITS as a
        // whole. A plain array store broadcasts the scalar into every element
        // (and rejects values wider than one element). Preserve the packed
        // assignment with the same whole-bit view used by lower_named_value.
        const auto& mi = mem_info_.at(&sym);
        builder_.create_set_mask_stmts(wname, to_int_value(rhs), "0", std::to_string(mi.size * mi.elem_bits));
      } else {
        builder_.create_assign_stmts(wname, to_int_value(rhs));
      }
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
    case ExpressionKind::RangeSelect  : {
      const auto& base    = lhs.kind == ExpressionKind::ElementSelect ? lhs.as<slang::ast::ElementSelectExpression>().value()
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

      // Every other sub-word element write: a combinational array (`tile_wr_data
      // [r][c][port] = …`), a struct-element memory, or a NON-constant in-word
      // position (`useful[i][decrBit] <= 0`) with no constant chunk to enable.
      // Read-modify-write the addressed element.
      if (lower_mem_element_splice_write(lhs, rhs)) {
        return;
      }

      // Constant LOW BIT offset of `lhs` inside its packed `base`, used by the
      // per-field bundle struct leaf path below. A packed-array element can be
      // wider than one bit, so the selector yields an element ORDINAL that must
      // be scaled by the element stride before it can mask a leaf — exactly
      // what resolve_packed_lvalue
      // does for the generic path, and what reads already do via lower_rvalue.
      // Returns nullopt when the position is not compile-time constant.
      const auto const_lane_lo = [&](int64_t sel_bits) -> std::optional<int64_t> {
        if (!base.type->isIntegral() || !base.type->hasFixedRange()) {
          return std::nullopt;
        }
        const int64_t stride
            = base_ty.isPackedArray() ? static_cast<int64_t>(base_ty.getArrayElementType()->getBitWidth()) : int64_t{1};
        if (stride <= 0) {
          return std::nullopt;
        }
        const auto    rng  = base.type->getFixedRange();
        const int64_t nsel = sel_bits / stride;  // selected ELEMENTS (1 for an element select)
        if (lhs.kind == ExpressionKind::ElementSelect) {
          auto ci = try_eval_int(lhs.as<slang::ast::ElementSelectExpression>().selector());
          if (!ci) {
            return std::nullopt;
          }
          return (rng.isDescending() ? (*ci - rng.lower()) : (rng.upper() - *ci)) * stride;
        }
        const auto& rs   = lhs.as<slang::ast::RangeSelectExpression>();
        const auto  kind = rs.getSelectionKind();
        if (kind == slang::ast::RangeSelectionKind::Simple) {
          auto l = try_eval_int(rs.left());
          auto r = try_eval_int(rs.right());
          if (!l || !r) {
            return std::nullopt;
          }
          return (rng.isDescending() ? (std::min(*l, *r) - rng.lower()) : (rng.upper() - std::max(*l, *r))) * stride;
        }
        auto b = try_eval_int(rs.left());  // `[base+:W]` / `[base-:W]` with constant base
        if (!b) {
          return std::nullopt;
        }
        if (kind == slang::ast::RangeSelectionKind::IndexedUp) {
          return (rng.isDescending() ? (*b - rng.lower()) : (rng.upper() - *b - (nsel - 1))) * stride;
        }
        return (rng.isDescending() ? (*b - rng.lower() - (nsel - 1)) : (rng.upper() - *b)) * stride;
      };

      // `struct.field[i] = v` / `struct.field[hi:lo] = v` on a PER-FIELD bundle
      // struct: a bit-write of the FIELD LEAF net. The flat whole-struct net
      // does not exist (fields were detupled), so rooting the RMW on the base
      // (the resolve_packed_lvalue fallback below) writes an undeclared net
      // nobody reads — recompile error AND lost behavior.
      if (base.kind == ExpressionKind::MemberAccess) {
        const auto& ma = base.as<slang::ast::MemberAccessExpression>();
        if (ma.value().kind == ExpressionKind::NamedValue && ma.member.kind == slang::ast::SymbolKind::Field) {
          const auto& bsym = ma.value().as<slang::ast::NamedValueExpression>().symbol;
          if (is_scalar_struct_var(bsym)) {
            if (!declared_.contains(&bsym)) {
              declare_value_symbol(bsym, /*force_reg=*/false);
            }
            const auto& field = ma.member.as<slang::ast::FieldSymbol>();
            if (auto it = struct_var_info_.find(&bsym); it != struct_var_info_.end()) {
              const auto* f = find_struct_field(it->second, field.name);
              if (f != nullptr && !it->second.is_tuple && base.type->isIntegral() && base.type->hasFixedRange()) {
                auto                   ti = tinfo(*lhs.type);
                std::optional<int64_t> lo = const_lane_lo(ti.bits);
                if (lo && *lo >= 0 && *lo + ti.bits <= f->bits) {
                  auto val = to_pattern(to_int_value(rhs), ti.bits, ti.is_signed);
                  note_write(bsym, current_assign_nonblocking_, lhs.sourceRange.start());
                  builder_.create_set_mask_stmts(absl::StrCat(lname_of(bsym), ".", f->name),
                                                 val,
                                                 std::to_string(*lo),
                                                 std::to_string(*lo + ti.bits));
                  return;
                }
              }
            }
          }
        }
      }

      // `structvar[i] = v` / `structvar[hi:lo] = v` on a PER-FIELD bundle
      // struct (the whole packed struct used as a bit vector): the flat
      // whole-struct net does not exist (fields were detupled), so rooting the
      // RMW on the base writes an undeclared net nobody reads — recompile
      // error AND lost behavior. Split the constant slice across every
      // overlapped FIELD LEAF (a span may cross field boundaries).
      if (base.kind == ExpressionKind::NamedValue && base.type->isIntegral() && base.type->hasFixedRange()) {
        const auto& bsym = base.as<slang::ast::NamedValueExpression>().symbol;
        if (is_scalar_struct_var(bsym)) {
          if (!declared_.contains(&bsym)) {
            declare_value_symbol(bsym, /*force_reg=*/false);
          }
          if (auto it = struct_var_info_.find(&bsym); it != struct_var_info_.end() && !it->second.is_tuple) {
            auto                   ti    = tinfo(*lhs.type);
            const int64_t          width = ti.bits;
            std::optional<int64_t> lo    = const_lane_lo(width);
            if (lo && *lo >= 0 && width > 0) {
              const int64_t hi = *lo + width - 1;
              note_write(bsym, current_assign_nonblocking_, lhs.sourceRange.start());
              auto val = to_pattern(to_int_value(rhs), static_cast<int>(width), ti.is_signed);
              for (const auto& f : it->second.fields) {
                const int64_t ov_lo = std::max<int64_t>(*lo, f.off);
                const int64_t ov_hi = std::min<int64_t>(hi, f.off + f.bits - 1);
                if (ov_lo > ov_hi) {
                  continue;  // field outside the written slice
                }
                const int   ov_bits = static_cast<int>(ov_hi - ov_lo + 1);
                std::string part  = ov_lo == *lo ? val : to_int_value(builder_.create_sra_stmts(val, std::to_string(ov_lo - *lo)));
                part              = to_pattern(part, ov_bits, false);
                const int64_t rel = ov_lo - f.off;  // LSB position within the field leaf
                builder_.create_set_mask_stmts(absl::StrCat(lname_of(bsym), ".", f.name),
                                               part,
                                               std::to_string(rel),
                                               std::to_string(rel + ov_bits));
              }
              return;
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
      emit_unsupported(lhs.sourceRange, "unsupported-lhs-nesting", "nested non-variable assignment targets are not supported yet");
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
        std::vector<const slang::ast::Expression*> sels;
        const auto&                                mbase    = *peel_unpacked_chain(ma.value(), sels);
        const auto*                                base_sym = resolve_base_symbol(mbase);
        if (base_sym != nullptr && !flat_port_syms_.contains(base_sym)) {
          auto mit = mem_info_.find(base_sym);
          if (mit != mem_info_.end() && mit->second.is_tuple && sels.size() == mit->second.rank()) {
            const auto  mi    = mit->second;  // a COPY: lowering a selector can rehash mem_info_
            const auto& field = ma.member.as<slang::ast::FieldSymbol>();
            if (const auto* f = find_tuple_field(mi, field.name)) {
              const auto addr = build_unpacked_address(mi, sels);
              note_write(*base_sym, current_assign_nonblocking_, lhs.sourceRange.start());
              const auto val = to_pattern(to_int_value(rhs), f->bits, f->is_signed);
              emit_if_in_range(addr, [&]() { emit_field_store(write_target_of(*base_sym), addr.index, f->name, val); });
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
      emit_unsupported(lhs.sourceRange, "unsupported-lhs-nesting", "nested non-variable assignment targets are not supported yet");
      return;
    }

    case ExpressionKind::SimpleAssignmentPattern:
      // `'{a, b, ...}` used as an assignment target. The only SV-legal form is an
      // unpacked-array output-port connection (the port type supplies the
      // pattern's context), e.g. soomrv's `.OUT_idx('{resLzTz})`.
      assign_to_pattern(lhs, lhs.as<slang::ast::SimpleAssignmentPatternExpression>().elements(), rhs);
      return;

    default:
      emit_unsupported(lhs.sourceRange,
                       "unsupported-lhs",
                       std::string("assignment-target kind '") + std::string(slang::ast::toString(lhs.kind))
                           + "' is not supported by --reader slang yet");
  }
}

// `'{...}` (SimpleAssignmentPattern) as an assignment target. slang lowers an
// unpacked-array output-port connection `.p('{a, b})` to `'{a, b} = <child
// output>`, so `rhs` is the child's flattened output bus. Decompose it per
// element and recurse:
//   - unpacked array target: source element k maps to array index left+k*step
//     (LRM: first listed element -> leftmost index); Yosys places the rightmost
//     element in the flat bus LSB, exactly matching flat_port_read/write.
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
  const auto fields   = it->second.fields;  // copy
  auto       base     = lname_of(sym);
  // Write field `f` of this struct: a wire struct is a real tuple (field-store
  // op, detuple-split); a mut struct is flat leaves.
  auto       put      = [&](const std::string& field, const std::string& value) {
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
  // element[i] is field[i]'s value — including for a STRUCTURED (`cause:`,
  // `interrupt_x:`, `default:`) pattern, whose named/type/default keys forStruct
  // has already expanded into one per-field element, each re-bound at that
  // field's type. Write each leaf from its element directly.
  std::span<const slang::ast::Expression* const> elems;
  bool                                           is_pattern = true;
  switch (r->kind) {
    case ExpressionKind::SimpleAssignmentPattern: elems = r->as<slang::ast::SimpleAssignmentPatternExpression>().elements(); break;
    case ExpressionKind::StructuredAssignmentPattern: {
      // One exception to "element[i] is field[i]": an `index:` key, which only a
      // packed-ARRAY pattern can carry and which slang orders ascending, not
      // MSB-first (see the guard in lower_rvalue's StructuredAssignmentPattern
      // case). Leave those to the whole-value slice below, which routes through
      // lower_rvalue and emits that diagnostic instead of a wrong per-leaf write.
      const auto& sap = r->as<slang::ast::StructuredAssignmentPatternExpression>();
      if (sap.indexSetters.empty()) {
        elems = sap.elements();
      } else {
        is_pattern = false;
      }
      break;
    }
    case ExpressionKind::ReplicatedAssignmentPattern:
      elems = r->as<slang::ast::ReplicatedAssignmentPatternExpression>().elements();
      break;
    default: is_pattern = false; break;
  }
  if (is_pattern && elems.size() == fields.size()) {
    note_write(sym, current_assign_nonblocking_, rhs.sourceRange.start());
    std::vector<size_t> emit_order;
    emit_order.reserve(fields.size());

    // A continuous whole-aggregate assignment evaluates as a set of
    // position-independent net equations.  Our leaf stores are textual, so
    // topologically order the acyclic sibling dependencies.  Do not apply
    // this to procedural blocking assignments: there, turning an old-value
    // read into a newly-written sibling read would change the language
    // semantics.  Cyclic remnants retain source order and are handled by the
    // existing wire/cycle machinery rather than being silently broken here.
    if (proc_kind_ == Proc_kind::none) {
      std::vector<std::vector<size_t>> users(fields.size());
      std::vector<size_t>              indegree(fields.size(), 0);
      for (size_t i = 0; i < fields.size(); ++i) {
        absl::flat_hash_set<std::string> reads;
        Self_field_read_collector        collector;
        collector.target = &sym;
        collector.reads  = &reads;
        elems[i]->visit(collector);
        for (const auto& name : reads) {
          for (size_t j = 0; j < fields.size(); ++j) {
            if (i != j && fields[j].name == name) {
              users[j].push_back(i);
              ++indegree[i];
              break;
            }
          }
        }
      }
      std::vector<bool> emitted(fields.size(), false);
      for (size_t n = 0; n < fields.size(); ++n) {
        size_t ready = fields.size();
        for (size_t i = 0; i < fields.size(); ++i) {
          if (!emitted[i] && indegree[i] == 0) {
            ready = i;  // stable among independent fields
            break;
          }
        }
        if (ready == fields.size()) {
          break;  // a genuine dependency cycle remains
        }
        emitted[ready] = true;
        emit_order.push_back(ready);
        for (size_t user : users[ready]) {
          --indegree[user];
        }
      }
      for (size_t i = 0; i < fields.size(); ++i) {
        if (!emitted[i]) {
          emit_order.push_back(i);
        }
      }
    } else {
      for (size_t i = 0; i < fields.size(); ++i) {
        emit_order.push_back(i);
      }
    }

    for (size_t i : emit_order) {
      const auto& f = fields[i];
      // fit_wrap (not to_pattern): land each value in the leaf's declared
      // width/sign — a signed field must stay in its signed range.
      auto        v = fit_wrap(to_int_value(lower_rvalue(*elems[i])), f.bits, f.is_signed);
      put(f.name, v);
    }
    return true;
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
        // Per-leaf copy is only valid when the source has the SAME field list
        // (names/offsets/widths). A bitcast between different struct types
        // (`ret = csr_sip_t'(mip)` with `mip : csr_mip_t` — the Conversion was
        // peeled above) must NOT read dest-named leaves off the source (they
        // do not exist); it falls through to the whole-value slice below.
        const auto& of         = oit->second.fields;
        bool        same_shape = of.size() == fields.size();
        for (size_t i = 0; same_shape && i < fields.size(); ++i) {
          same_shape = of[i].name == fields[i].name && of[i].off == fields[i].off && of[i].bits == fields[i].bits;
        }
        if (same_shape) {
          const bool o_is_tuple = oit->second.is_tuple;
          note_write(sym, current_assign_nonblocking_, rhs.sourceRange.start());
          for (const auto& f : fields) {
            // Read the source field per the SOURCE struct's representation.
            auto src
                = o_is_tuple ? read_struct_field_get(lname_of(osym), f.name) : read_leaf(absl::StrCat(lname_of(osym), ".", f.name));
            put(f.name, src);
          }
          return true;
        }
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

// M7: whole write to a BUNDLE port — slice the flat value onto the field
// leaves (mirror assign_struct_whole_value; the tuple port has no flat net).
bool Slang_context::assign_bundle_port_whole_value(const slang::ast::ValueSymbol& sym, const std::string& value,
                                                   slang::SourceLocation loc) {
  auto it = bundle_port_info_.find(&sym);
  if (it == bundle_port_info_.end()) {
    return false;
  }
  const auto fields = it->second.fields;  // copy: builder calls can rehash the map
  auto       base   = bundle_port_body_base(sym);
  auto       bi     = tinfo(sym.getType());
  auto       p      = to_pattern(to_int_value(value), bi.bits, false);
  note_write(sym, current_assign_nonblocking_, loc);
  for (const auto& f : fields) {
    auto fv = extract_field(p, f.off, f.bits);
    if (f.is_signed) {
      fv = builder_.create_sext_stmts(fv, std::to_string(f.bits - 1));
    }
    emit_leaf_store(absl::StrCat(base, ".", f.name), fv);
  }
  return true;
}

// A partial (bit-slice) write whose root is stored as per-field LEAVES has no
// flat net to set_mask: an M7 BUNDLE port, or a per-field struct var (whose
// flat name is a phantom nothing reads -- a write to it was silently lost).
// The chain is already collapsed to (base, offset, width) by
// resolve_packed_lvalue. Constant offsets split the span per overlapped field
// (full cover = plain leaf store, partial cover = splice of the field leaf --
// the same shape the struct-var leaf branch emits). A dynamic offset splices
// a copy of the fields the write can reach and writes those fields back:
// rewriting the others too would give them another driver.
void Slang_context::emit_leaf_split_rmw(const Packed_lv& lv, const std::string& rhs, slang::SourceRange sr) {
  const auto& sym  = *lv.base;
  const auto* port = bundle_port_of(sym);
  const auto  sit  = struct_var_info_.find(&sym);
  if (port == nullptr && sit == struct_var_info_.end()) {
    return;  // caller guards; defensive
  }
  // Copies: builder calls below can rehash both maps.
  const bool is_port    = port != nullptr;
  const auto fields     = is_port ? port->fields : sit->second.fields;
  const bool is_tuple   = !is_port && sit->second.is_tuple;
  const auto base       = is_port ? bundle_port_body_base(sym) : lname_of(sym);
  const auto rbase      = is_port ? bundle_port_read_base(sym) : base;
  auto       val        = to_pattern(rhs, static_cast<int>(lv.width), lv.is_signed);
  const auto read_field = [&](const Struct_info::Field& f) {
    return is_tuple ? read_struct_field_get(rbase, f.name) : read_leaf(absl::StrCat(rbase, ".", f.name));
  };
  const auto store_field = [&](const Struct_info::Field& f, const std::string& v) {
    if (is_tuple) {
      emit_struct_field_set(base, f.name, v);  // tuple field store
    } else {
      emit_leaf_store(absl::StrCat(base, ".", f.name), v);
    }
  };

  if (!lv.dyn_off.empty()) {
    std::vector<Struct_info::Field> reached;
    std::string                     cur;  // the reached fields, at their root offsets
    for (const auto& f : fields) {
      if (f.off > lv.reach_hi || f.off + f.bits - 1 < lv.reach_lo) {
        continue;
      }
      auto placed = to_pattern(read_field(f), f.bits, f.is_signed);
      if (f.off != 0) {
        placed = builder_.create_shl_stmts(placed, std::to_string(f.off));
      }
      cur = cur.empty() ? placed : builder_.create_bit_or_stmts({cur, placed});
      reached.push_back(f);
    }
    if (reached.empty()) {
      return;
    }
    const auto dr = dynamic_write_range(lv, val);
    note_write(sym, current_assign_nonblocking_, sr.start());
    If_in_range lands(builder_, dr.in_range);
    const auto  next = splice_range(cur, dr.lo, dr.hi, dr.piece);
    for (const auto& f : reached) {
      auto fv = extract_field(next, f.off, f.bits);
      store_field(f, f.is_signed ? builder_.create_sext_stmts(fv, std::to_string(f.bits - 1)) : fv);
    }
    return;
  }

  int64_t lo = lv.const_off;
  if (lo < 0) {
    emit_warning(sr, "select-out-of-range", "bitwidth", "constant select is out of the declared range");
    lo = 0;
  }
  const int64_t hi = lo + lv.width - 1;
  note_write(sym, current_assign_nonblocking_, sr.start());
  for (const auto& f : fields) {
    const int64_t ov_lo = std::max<int64_t>(lo, f.off);
    const int64_t ov_hi = std::min<int64_t>(hi, f.off + f.bits - 1);
    if (ov_lo > ov_hi) {
      continue;  // field outside the written slice
    }
    const int   ov_bits = static_cast<int>(ov_hi - ov_lo + 1);
    std::string part    = ov_lo == lo ? val : to_int_value(builder_.create_sra_stmts(val, std::to_string(ov_lo - lo)));
    part                = to_pattern(part, ov_bits, false);
    const int64_t rel   = ov_lo - f.off;  // LSB position within the field leaf
    if (rel == 0 && ov_bits == f.bits) {
      // A direct full-leaf store already has the leaf's exact width and needs
      // no mask. When splitting a wider packed value, select the low field as
      // the explicit precision-changing part of that split.
      if (lv.width > ov_bits) {
        part = trunc_to(part, ov_bits);
      }
      store_field(f, f.is_signed ? builder_.create_sext_stmts(part, std::to_string(f.bits - 1)) : part);
      continue;
    }
    if (is_tuple) {
      // A tuple field has no in-place bit write: splice a copy of it.
      store_field(f,
                  fit_wrap(splice_range(to_pattern(read_field(f), f.bits, f.is_signed),
                                        std::to_string(rel),
                                        std::to_string(rel + ov_bits - 1),
                                        part),
                           f.bits,
                           f.is_signed));
    } else {
      builder_.create_set_mask_stmts(absl::StrCat(base, ".", f.name), part, std::to_string(rel), std::to_string(rel + ov_bits));
    }
  }
}

void Slang_context::assign_to_pattern(const slang::ast::Expression& lhs, std::span<const slang::ast::Expression* const> elems,
                                      const std::string& rhs) {
  const auto& ct = lhs.type->getCanonicalType();

  if (!ct.isIntegral() && ct.kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
    const auto&   arr  = ct.as<slang::ast::FixedSizeUnpackedArrayType>();
    const int     eb   = flat_or_tinfo(arr.elementType).bits;
    const int     flat = eb * static_cast<int>(arr.range.width());
    const int64_t left = arr.range.left;
    const int64_t step = arr.range.right >= arr.range.left ? 1 : -1;
    auto          p    = to_pattern(to_int_value(rhs), flat, false);
    int64_t       k    = 0;
    for (const auto* e : elems) {
      const int64_t idx    = left + k * step;
      const int64_t lo_bit = (arr.range.isDescending() ? idx - arr.range.lower() : arr.range.upper() - idx) * eb;
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
    auto oi  = tinfo(*e->type);
    offset  -= oi.bits;
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

// Nested `m[i][j]` selects on a multi-dim unpacked array: collect the selector
// chain (reversed to OUTERMOST dim first — `(m[i])[j]`'s outer select carries
// the innermost dim) and return the expression below it. The loop also accepts
// the single select on a memory-ized packed 2-D reg base (packed base ends the
// walk after one selector).
const slang::ast::Expression* Slang_context::peel_unpacked_chain(const slang::ast::Expression&               expr,
                                                                 std::vector<const slang::ast::Expression*>& sels) {
  const auto* e = &expr;
  while (e->kind == ExpressionKind::ElementSelect) {
    const auto& es = e->as<slang::ast::ElementSelectExpression>();
    sels.push_back(&es.selector());
    e = &es.value();
    if (!e->type->getCanonicalType().isUnpackedArray()) {
      break;
    }
  }
  std::reverse(sels.begin(), sels.end());
  return e;
}

// Linear 0-based element address of a selector prefix (all of the dims for
// an element access): row-major accumulate `acc = acc*width_k + (sel_k -
// lower_k)`, folding while every term is a compile-time constant. A Mem_info
// without dims (memory-ized packed reg) is a single dim {lower, size}.
//
// A selector whose TYPE reaches past its dim's declared bounds makes the
// access a possible Verilog out-of-range one (1800 7.4.6: a write does
// nothing, a read returns X). The address then carries that range check and
// an index cut to the low bits that address the span, so it is never negative
// (see Unpacked_address). The check is the Verilog semantics spelled out in
// the LNAST: the lowered memory cannot see it (it keeps the low address bits,
// so an unguarded `mem[10] <= x` of `mem [2:9]` would overwrite mem[2]), and
// the Pyrope regenerated from this LNAST states it as a guard.
Slang_context::Unpacked_address Slang_context::build_unpacked_address(const Mem_info&                                   mi,
                                                                      const std::vector<const slang::ast::Expression*>& sels) {
  Unpacked_address addr;
  bool             never = false;  // a constant selector outside its dim
  auto             check = [&](const std::string& condition) {
    addr.in_range = addr.in_range.empty() ? condition : builder_.create_log_and_stmts(addr.in_range, condition);
  };
  std::optional<int64_t> cacc = 0;  // constant accumulator while it stays foldable
  std::string            dacc;      // otherwise the accumulated expression
  for (size_t k = 0; k < sels.size(); ++k) {
    const auto d  = mi.dims.empty() ? Mem_info::Dim{mi.lower, mi.size} : mi.dims[k];
    addr.span    *= d.width;
    if (k > 0) {
      if (cacc) {
        *cacc *= d.width;
      } else if (d.width != 1) {
        dacc = builder_.create_mult_stmts(dacc, std::to_string(d.width));
      }
    }
    if (auto ci = try_eval_int(*sels[k])) {
      never = never || *ci < d.lower || *ci - d.lower >= d.width;
      if (cacc) {
        *cacc += *ci - d.lower;
      } else if (*ci != d.lower) {
        dacc = builder_.create_plus_stmts(dacc, std::to_string(*ci - d.lower));
      }
    } else {
      auto       v     = to_int_value(lower_rvalue(*sels[k]));
      const auto ti    = tinfo(*sels[k]->type);
      // The selector's own type bounds its value; only a bound it can cross
      // needs a compare (an exact-width `[0:2^k-1]` index needs none).
      const bool below = ti.is_signed ? d.lower > -(int64_t{1} << std::min(ti.bits - 1, 62)) : d.lower > 0;
      const bool above = ti.bits - (ti.is_signed ? 1 : 0) >= 63
                         || d.lower + d.width - 1 < (int64_t{1} << (ti.bits - (ti.is_signed ? 1 : 0))) - 1;
      if (below) {
        check(builder_.create_ge_stmts(v, std::to_string(d.lower)));
      }
      if (above) {
        check(builder_.create_lt_stmts(v, std::to_string(d.lower + d.width)));
      }
      if (d.lower != 0) {
        v = builder_.create_minus_stmts(v, std::to_string(d.lower));
      }
      if (cacc) {
        dacc = *cacc == 0 ? v : builder_.create_plus_stmts(std::to_string(*cacc), v);
        cacc.reset();
      } else {
        dacc = builder_.create_plus_stmts(dacc, v);
      }
    }
  }
  if (never) {
    addr.in_range = "false";
  }
  // The index an out-of-range access lands on: the low bits addressing the
  // span, like the memory's own address decode.
  int abits = 0;
  while ((int64_t{1} << abits) < addr.span) {
    ++abits;
  }
  if (cacc) {
    const int64_t low = *cacc & ((int64_t{1} << abits) - 1);  // two's complement: the low bits of a negative offset
    addr.index        = std::to_string(low);
    addr.past_span    = low >= addr.span;
    addr.constant     = true;
    return addr;
  }
  if (addr.in_range.empty()) {
    addr.index = dacc;  // provably inside the declared bounds
    return addr;
  }
  addr.index     = abits == 0 ? std::string("0") : trunc_to(dacc, abits);
  addr.past_span = (int64_t{1} << abits) != addr.span;
  return addr;
}

// Emit `emit` (the stores of a write) under the address check: nothing for a
// constant out-of-range address, `if in_range { ... }` for a runtime one.
void Slang_context::emit_if_in_range(const Unpacked_address& addr, const std::function<void()>& emit) {
  if (addr.in_range == "false") {
    return;  // Verilog drops an out-of-range write
  }
  If_in_range guard(builder_, addr.in_range);
  emit();
}

// Read one element through `read(index)`. An out-of-range read is X in
// Verilog; the refinement is the element the low address bits select (what
// the memory hardware and yosys' memory_map return, so every engine agrees),
// and 0 when those bits land past a span that is not a power of two.
std::string Slang_context::emit_guarded_read(const Unpacked_address& addr, int bits,
                                             const std::function<std::string(const std::string&)>& read) {
  if (!addr.past_span) {
    return read(addr.index);
  }
  if (addr.constant) {
    return "0";  // a constant index past the span
  }
  // declare + store, like prp2lnast's `mut t:uN = 0`: tolg lowers no value
  // from a `mut` declare's init child, so an init-only 0 would leave the mux's
  // false arm undriven (cgen X, lhd lec 0).
  auto result = fresh_local("oob_read");
  builder_.create_declare_stmts(result, "mut", mask_text(bits), "0");
  builder_.create_assign_stmts(result, "0");
  If_in_range guard(builder_, builder_.create_lt_stmts(addr.index, std::to_string(addr.span)));
  builder_.create_assign_stmts(result, read(addr.index));
  return result;
}

// Unpacked-array (memory) element access (2s-D). Reads lower to
// tuple_get(dst, mem, idx) = one read port per site; writes to the 3-child
// store(mem, idx, val) = one write port per site (enable = branch path
// condition, wired by tolg). Indices are biased by the declared lower bound;
// a multi-dim array folds its full selector chain to one linear index.
std::string Slang_context::lower_unpacked_read(const slang::ast::Expression& expr) {
  if (expr.kind != ExpressionKind::ElementSelect) {
    emit_unsupported(expr.sourceRange,
                     "unsupported-array-read",
                     "only single-element reads of unpacked arrays are supported by --reader slang");
    return "0";
  }
  std::vector<const slang::ast::Expression*> sels;
  const auto&                                base     = *peel_unpacked_chain(expr, sels);
  const auto*                                base_sym = resolve_base_symbol(base);
  if (base_sym != nullptr && flat_port_syms_.contains(base_sym) && sels.size() == 1) {
    return flat_port_read(expr.as<slang::ast::ElementSelectExpression>(), mem_info_.at(base_sym));
  }
  auto mit = base_sym != nullptr ? mem_info_.find(base_sym) : mem_info_.end();
  if (mit == mem_info_.end() || sels.size() > mit->second.rank()) {
    emit_unsupported(expr.sourceRange, "unsupported-array-read", "unpacked array read on an unsupported base");
    return "0";
  }
  const auto mi           = mit->second;
  const auto addr         = build_unpacked_address(mi, sels);
  const auto mem_name     = write_target_of(*base_sym);
  auto       read_element = [&](const std::string& index) {
    if (mi.is_tuple) {
      std::string acc;
      for (const auto& f : mi.fields) {
        auto fv     = emit_field_read_chain(mem_name, index, f.name);
        auto placed = to_pattern(fv, mi.elem_bits, false);
        if (f.off != 0) {
          placed = builder_.create_shl_stmts(placed, std::to_string(f.off));
        }
        acc = acc.empty() ? placed : builder_.create_bit_or_stmts({acc, placed});
      }
      return acc.empty() ? std::string("0") : acc;
    }
    auto& ln  = *builder_.lnast;
    auto  tg  = builder_.add_child(Lnast_ntype::create_tuple_get());
    auto  tmp = builder_.create_lnast_tmp();
    ln.add_child(tg, Lnast_node::create_ref(tmp));
    ln.add_child(tg, Lnast_node::create_ref(mem_name));
    builder_.add_value_child_pub(tg, index);
    builder_.note_unsigned_bits(tmp, mi.elem_bits);
    return tmp;
  };
  if (sels.size() == mi.rank()) {
    auto value = emit_guarded_read(addr, mi.elem_bits, read_element);
    return mi.elem_signed ? builder_.create_sext_stmts(value, std::to_string(mi.elem_bits - 1)) : value;
  }

  // A partial selector denotes a value array, not one memory element. Gather
  // its leaves in declaration order for the flat function / instance port ABI.
  // Native memory indices instead count upward from each numeric lower bound;
  // walk the remaining type ranges to bridge both ascending and descending axes.
  int64_t count = 1;
  for (size_t d = sels.size(); d < mi.rank(); ++d) {
    count *= mi.dims[d].width;
  }
  if (count > 65536 / mi.elem_bits) {
    emit_unsupported(expr.sourceRange, "array-value-too-wide", "unpacked array value exceeds 65536 bits");
    return "0";
  }
  auto gather_value = [&]() {
    const auto                              idx = builder_.create_mult_stmts(addr.index, std::to_string(count));
    std::vector<Lnast_builder::Concat_lane> lanes;
    std::function<void(const slang::ast::Type&, int64_t, int64_t)> gather = [&](const auto& type, int64_t offset, int64_t stride) {
      const auto& ct = type.getCanonicalType();
      if (ct.kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
        const auto& arr  = ct.template as<slang::ast::FixedSizeUnpackedArrayType>();
        stride          /= arr.range.width();
        for (int64_t k = 0; k < arr.range.width(); ++k) {
          auto lane = arr.range.isDescending() ? arr.range.width() - 1 - k : k;
          gather(arr.elementType, offset + lane * stride, stride);
        }
      } else {
        auto index = offset == 0 ? idx : builder_.create_plus_stmts(idx, std::to_string(offset));
        lanes.push_back({read_element(index), mi.elem_bits});
      }
    };
    gather(*expr.type, 0, count);
    return builder_.create_concat_stmts(lanes);
  };
  if (addr.in_range.empty()) {
    return gather_value();
  }
  // An out-of-range row reads X. The element reads sit under the check too, so
  // none of them addresses past the memory.
  auto result = fresh_local("array_value");
  builder_.create_declare_stmts(result, "mut", mask_text(static_cast<int>(count * mi.elem_bits)), "0", "0sb?");
  emit_if_in_range(addr, [&]() { builder_.create_assign_stmts(result, gather_value()); });
  return result;
}

void Slang_context::lower_unpacked_write(const slang::ast::Expression& lhs, const std::string& rhs) {
  if (lhs.kind != ExpressionKind::ElementSelect) {
    emit_unsupported(lhs.sourceRange,
                     "unsupported-array-write",
                     "only single-element writes of unpacked arrays are supported by --reader slang");
    return;
  }
  std::vector<const slang::ast::Expression*> sels;
  const auto&                                base     = *peel_unpacked_chain(lhs, sels);
  const auto*                                base_sym = resolve_base_symbol(base);
  if (base_sym != nullptr && flat_port_syms_.contains(base_sym) && sels.size() == 1) {
    flat_port_write(lhs.as<slang::ast::ElementSelectExpression>(), mem_info_.at(base_sym), rhs);
    return;
  }
  auto mit = base_sym != nullptr ? mem_info_.find(base_sym) : mem_info_.end();
  if (mit == mem_info_.end() || sels.size() != mit->second.rank()) {
    emit_unsupported(lhs.sourceRange, "unsupported-array-write", "unpacked array write on an unsupported base");
    return;
  }
  const auto mi = mit->second;  // a COPY: lowering a selector can rehash mem_info_

  const auto addr = build_unpacked_address(mi, sels);

  // Struct-element memory whole-element write `mem[idx] <= rhs`: decompose into
  // one field store per field. `rhs` is the packed element value (whatever its
  // origin — struct literal, 'x, packed port bus, another element), so each
  // field's value is the matching bit-slice. detuple routes each to mem.field.
  if (mi.is_tuple) {
    note_write(*base_sym, current_assign_nonblocking_, lhs.sourceRange.start());
    auto p        = to_pattern(to_int_value(rhs), mi.elem_bits, false);
    auto mem_name = write_target_of(*base_sym);
    emit_if_in_range(addr, [&]() {
      for (const auto& f : mi.fields) {
        emit_field_store(mem_name, addr.index, f.name, extract_field(p, f.off, f.bits));
      }
    });
    return;
  }

  auto val = to_pattern(to_int_value(rhs), mi.elem_bits, mi.elem_signed);

  note_write(*base_sym, current_assign_nonblocking_, lhs.sourceRange.start());

  emit_if_in_range(addr, [&]() {
    auto& ln = *builder_.lnast;
    auto  st = builder_.add_child(Lnast_ntype::create_store());
    ln.add_child(st, Lnast_node::create_ref(write_target_of(*base_sym)));
    builder_.add_value_child_pub(st, addr.index);
    builder_.add_value_child_pub(st, val);
  });
}

// The BASE of a sub-word element write (`mem[addr]`, `mem[r][c]`, …) resolved to
// its memory. Uses the same peel_unpacked_chain walk as the read side, so a
// MULTI-dimensional array is handled exactly like a one-dimensional one: the
// linearizing index math lives in build_unpacked_address and is emitted by the
// caller (this function is on paths that still return false, so it must not
// emit LNAST).
const slang::ast::ValueSymbol* Slang_context::resolve_mem_element_base(const slang::ast::Expression&               base,
                                                                       std::vector<const slang::ast::Expression*>& sels,
                                                                       Mem_info&                                   mi_out) {
  if (base.kind != ExpressionKind::ElementSelect) {
    return nullptr;
  }
  const auto& root    = *peel_unpacked_chain(base, sels);
  const auto* mem_sym = resolve_base_symbol(root);
  if (mem_sym == nullptr || flat_port_syms_.contains(mem_sym)) {
    return nullptr;  // flat-port arrays are bit-slices of a packed bus, not memories
  }
  // The chain must index a single memory element: an UNPACKED-array memory, OR a
  // memory-ized packed 2-D reg (register file, `logic [N:0][W:0][..]`). Both
  // store one word per index; the caller's write targets a sub-chunk of that
  // word. The read path (lower_rvalue) already routes packed_mem_regs_ here.
  if (!root.type->getCanonicalType().isUnpackedArray() && !packed_mem_regs_.contains(mem_sym)) {
    return nullptr;
  }
  auto mit = mem_info_.find(mem_sym);
  if (mit == mem_info_.end() || sels.size() != mit->second.rank()) {
    return nullptr;  // a partial selector chain does not name one element
  }
  mi_out = mit->second;  // COPY: the callers lower selector expressions, which can rehash mem_info_
  return mem_sym;
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

  // The slice base must select one memory element: `mem[addr]`, or a full
  // selector chain of a multi-dimensional array (`mem[r][c]`).
  const auto& base = lhs.kind == ExpressionKind::ElementSelect ? lhs.as<slang::ast::ElementSelectExpression>().value()
                                                               : lhs.as<slang::ast::RangeSelectExpression>().value();
  std::vector<const slang::ast::Expression*> sels;
  Mem_info                                   mi;
  const auto*                                mem_sym = resolve_mem_element_base(base, sels, mi);
  if (mem_sym == nullptr) {
    return false;
  }
  // The chunk model is a MEMORY's per-chunk write ENABLE. Two shapes cannot
  // carry it, and both take the read-modify-write splice instead:
  //  * an array tolg represents as a packed BUS rather than a memory (see
  //    lowers_as_memory) — its store takes exactly (index, value), so the chunk
  //    child is a hard error there;
  //  * a struct-element (TUPLE) memory — its storage is the per-field arrays
  //    detuple splits out, so a store on the aggregate name writes a net that
  //    does not exist (it was silently dropped before this guard).
  if (!lowers_as_memory(*mem_sym) || mi.is_tuple) {
    return false;
  }
  const int word_bits = mi.elem_bits;

  // The element word's type (e.g. `reg [3:0]`) gives the in-word bit indexing.
  const auto& elem_ty = base.type->getCanonicalType();
  if (!elem_ty.isIntegral() || !elem_ty.hasFixedRange()) {
    return false;
  }
  auto          range  = elem_ty.getFixedRange();
  auto          ti     = tinfo(*lhs.type);
  const int64_t width  = ti.bits;
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
      // `*b` is an ELEMENT index of the packed word, so the far-end bias is the
      // selected ELEMENT count, not the selection's bit width. They differ
      // exactly when the element word is a packed ARRAY (`[3:0][1:0]`), and
      // getting it wrong silently writes a different chunk.
      const int64_t w = width / stride;
      if (kind == RangeSelectionKind::IndexedUp) {
        lo_bit = range.isDescending() ? (*b - range.lower()) : (range.upper() - *b - (w - 1));
      } else {  // IndexedDown
        lo_bit = range.isDescending() ? (*b - range.lower() - (w - 1)) : (range.upper() - *b);
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

  // ONE granularity per memory: the wensize attr is emitted once and every
  // chunk index counts in units of the first chunked write's width, so a slice
  // of another width (`[3:0]` next to `[15:8]`) would enable the wrong lane. It
  // takes the read-modify-write splice instead. A whole-word slice is a plain
  // write under any granularity.
  bool first_chunked = false;
  if (wensize > 1) {
    const auto [cit, fresh] = mem_chunk_bits_.try_emplace(mem_sym, width);
    if (!fresh && cit->second != width) {
      return false;
    }
    first_chunked = fresh;
  }

  // Position the chunk data within the full word (the other chunks are
  // don't-care — their write-enable bit is 0). din is the full element width.
  auto        val = to_pattern(rhs, static_cast<int>(width), ti.is_signed);
  std::string din = *lo_bit == 0 ? val : builder_.create_shl_stmts(val, std::to_string(*lo_bit));
  din             = to_pattern(to_int_value(din), word_bits, false);

  // Emit the per-memory wensize attr once (consumed by tolg's finalize_mems).
  if (first_chunked) {
    auto& ln   = *builder_.lnast;
    auto  aidx = builder_.add_child(Lnast_ntype::create_attr_set());
    ln.add_child(aidx, Lnast_node::create_ref(write_target_of(*mem_sym)));
    ln.add_child(aidx, Lnast_node::create_const("wensize"));
    ln.add_child(aidx, Lnast_node::create_const(std::to_string(wensize)));
  }

  // Memory write port: store(mem, addr, din, chunk). The extra chunk child
  // (D+2 store children) marks a chunked write to tolg.
  const auto addr = build_unpacked_address(mi, sels);
  note_write(*mem_sym, current_assign_nonblocking_, lhs.sourceRange.start());
  emit_if_in_range(addr, [&]() {
    auto& ln = *builder_.lnast;
    auto  st = builder_.add_child(Lnast_ntype::create_store());
    ln.add_child(st, Lnast_node::create_ref(write_target_of(*mem_sym)));
    builder_.add_value_child_pub(st, addr.index);
    builder_.add_value_child_pub(st, din);
    if (wensize > 1) {
      builder_.add_value_child_pub(st, std::to_string(chunk));
    }
  });
  return true;
}

// `mem[addr][bit/slice] = data` for every sub-word element write the chunked
// wensize model above cannot express. Read the addressed element, splice the new
// bits in, write the whole element back — the SEQUENCE of plain statements a
// constant-indexed unrolled loop means, which is sound precisely because the
// element being written is the only thing read back.
//
// Three callers land here, and the read they issue means different things:
//  * a COMBINATIONAL array. tolg keeps it as one packed bus, so the read sees
//    the in-flight value and successive partial writes ACCUMULATE in program
//    order — exactly Verilog net semantics. This is the only shape a chunk-
//    tagged store cannot take at all (tolg's scalar-view store is (index,
//    value) only), so const and dynamic in-word positions both come here.
//  * a struct-element (TUPLE) memory. Its storage is the per-field arrays
//    detuple splits out, so the write is decomposed per OVERLAPPED field: a
//    fully covered field is a plain field store, a partially covered one a
//    field-local splice. Only the partial cover reads back (before this
//    decomposition existed, the whole write was dropped instead).
//  * a CLOCKED scalar memory whose position the wensize path above could not
//    take: a DYNAMIC one (`useful[i][decrBit] <= 0` — no constant chunk to
//    enable) or a constant slice whose width does not divide the element word
//    evenly, is not chunk-aligned, or differs from the memory's chunk width.
//    Either position is the partial-write chain upass.tolg merges with every
//    earlier write of the cycle.
bool Slang_context::lower_mem_element_splice_write(const slang::ast::Expression& lhs, const std::string& rhs) {
  using slang::ast::RangeSelectionKind;

  const auto& base = lhs.kind == ExpressionKind::ElementSelect ? lhs.as<slang::ast::ElementSelectExpression>().value()
                                                               : lhs.as<slang::ast::RangeSelectExpression>().value();
  std::vector<const slang::ast::Expression*> sels;
  Mem_info                                   mi;
  const auto*                                mem_sym = resolve_mem_element_base(base, sels, mi);
  if (mem_sym == nullptr) {
    return false;
  }
  const auto& elem_ty = base.type->getCanonicalType();  // the element word type
  if (!elem_ty.isIntegral() || !elem_ty.hasFixedRange()) {
    return false;
  }
  auto      range = elem_ty.getFixedRange();
  auto      ti    = tinfo(*lhs.type);
  const int width = ti.bits;
  if (width <= 0 || width > mi.elem_bits) {
    return false;
  }
  // A packed element word (`[3:0][1:0]`) indexes ELEMENTS spanning `stride`
  // bits; a flat word (`[31:0]`) has stride 1. Same scaling as the chunked path.
  const int stride = elem_ty.isPackedArray() ? static_cast<int>(elem_ty.getArrayElementType()->getBitWidth()) : 1;

  // Constant in-word low bit, when the position folds.
  std::optional<int64_t> lo_bit;
  if (lhs.kind == ExpressionKind::ElementSelect) {
    if (auto ci = try_eval_int(lhs.as<slang::ast::ElementSelectExpression>().selector())) {
      lo_bit = range.isDescending() ? (*ci - range.lower()) : (range.upper() - *ci);
    }
  } else {
    const auto&   rs   = lhs.as<slang::ast::RangeSelectExpression>();
    auto          kind = rs.getSelectionKind();
    const int64_t w    = width / stride;  // selected ELEMENTS of the packed word
    if (kind == RangeSelectionKind::Simple) {
      auto l = try_eval_int(rs.left());
      auto r = try_eval_int(rs.right());
      if (l && r) {
        lo_bit = range.isDescending() ? std::min(*l, *r) - range.lower() : range.upper() - std::max(*l, *r);
      }
    } else if (auto b = try_eval_int(rs.left())) {
      if (kind == RangeSelectionKind::IndexedUp) {
        lo_bit = range.isDescending() ? (*b - range.lower()) : (range.upper() - *b - (w - 1));
      } else {
        lo_bit = range.isDescending() ? (*b - range.lower() - (w - 1)) : (range.upper() - *b);
      }
    }
  }
  if (lo_bit) {
    *lo_bit *= stride;  // element index -> bit offset within the word
    if (*lo_bit < 0 || *lo_bit + width > mi.elem_bits) {
      return false;
    }
  }

  // A tuple memory needs a constant position: a runtime in-word offset would
  // have to splice across an unknown field boundary.
  //
  // A COMBINATIONAL array that tolg still keeps as a Memory (it carries `initial`
  // power-on contents, so it is not the packed-bus scalar view) cannot take the
  // read-back at all — tolg rejects a read after a write on a mut memory. Leave
  // its constant positions to the chunk path above and, when that declines the
  // granularity, to the reader's own diagnostic, which names the real cause.
  const bool comb_memory = !reg_syms_.contains(mem_sym) && lowers_as_memory(*mem_sym);
  if (mi.is_tuple ? !lo_bit : (comb_memory && lo_bit.has_value())) {
    return false;
  }

  // Dynamic in-word low-bit offset. Mirrors resolve_packed_lvalue's normalize,
  // which a memory-element base makes unreachable on the packed path.
  // `lo_bounds` is its static range from the selector's type.
  std::optional<std::pair<int64_t, int64_t>> lo_bounds;
  auto offset_of = [&](const slang::ast::Expression& sel, int64_t wdown, int64_t wup) -> std::string {
    auto v = to_int_value(lower_rvalue(sel));
    if (range.isDescending()) {
      int64_t bias = range.lower() + (wdown - 1);
      lo_bounds    = affine_bounds(selector_bounds(sel), -bias, 1);
      return bias == 0 ? v : builder_.create_minus_stmts(v, std::to_string(bias));
    }
    int64_t bias = range.upper() - (wup - 1);
    lo_bounds    = affine_bounds(selector_bounds(sel), bias, -1);
    return builder_.create_minus_stmts(std::to_string(bias), v);
  };
  std::string dyn_lo;
  if (!lo_bit) {
    if (lhs.kind == ExpressionKind::ElementSelect) {
      dyn_lo = offset_of(lhs.as<slang::ast::ElementSelectExpression>().selector(), 1, 1);
    } else {
      const auto&   rs   = lhs.as<slang::ast::RangeSelectExpression>();
      auto          kind = rs.getSelectionKind();
      const int64_t w    = width / stride;  // ELEMENTS, like the constant branch above
      if (kind == RangeSelectionKind::IndexedUp) {
        dyn_lo = offset_of(rs.left(), 1, w);
      } else if (kind == RangeSelectionKind::IndexedDown) {
        dyn_lo = offset_of(rs.left(), w, 1);
      } else {
        return false;  // a Simple range with non-constant bounds: unsupported here
      }
    }
    if (stride != 1) {
      dyn_lo    = builder_.create_mult_stmts(dyn_lo, std::to_string(stride));
      lo_bounds = scaled_bounds(lo_bounds, stride);
    }
  }

  // Linear element index, computed once for the read(s) and the store(s). Every
  // early return is behind us, so emitting LNAST is safe from here on.
  const auto addr     = build_unpacked_address(mi, sels);
  const auto idx      = addr.index;
  auto       val      = to_pattern(rhs, width, ti.is_signed);
  auto&      ln       = *builder_.lnast;
  auto       mem_name = write_target_of(*mem_sym);

  // set_mask(%new, src, mask, value) — the copy-temp shape (dst != src) tolg
  // lowers without rebinding `src`, which here is a read temp, not a variable.
  // It is the read -> set_mask -> store chain upass.tolg recognizes as a
  // partial write: the read sees the writes of the cycle before it, so any
  // number of partial writes to one entry merge in program order.
  auto splice_const = [&](const std::string& src, int64_t lo, int bits, const std::string& piece) {
    return splice_range(src, std::to_string(lo), std::to_string(lo + bits - 1), piece);
  };

  note_write(*mem_sym, current_assign_nonblocking_, lhs.sourceRange.start());
  if (addr.in_range == "false") {
    return true;  // Verilog drops an out-of-range write
  }
  // The read-back and the store both sit under the address check.
  If_in_range in_range(builder_, addr.in_range);

  // Struct-element memory: split the constant slice across every OVERLAPPED
  // field leaf (a slice may cross field boundaries), the same decomposition the
  // packed-struct VARIABLE path uses.
  if (mi.is_tuple) {
    const int64_t hi = *lo_bit + width - 1;
    for (const auto& f : mi.fields) {
      const int64_t ov_lo = std::max<int64_t>(*lo_bit, f.off);
      const int64_t ov_hi = std::min<int64_t>(hi, f.off + f.bits - 1);
      if (ov_lo > ov_hi) {
        continue;  // field outside the written slice
      }
      const int     ov_bits = static_cast<int>(ov_hi - ov_lo + 1);
      const int64_t rel     = ov_lo - f.off;  // LSB position within the field leaf
      auto          piece   = extract_field(val, ov_lo - *lo_bit, ov_bits);
      if (ov_bits == f.bits) {
        emit_field_store(mem_name, idx, f.name, piece);  // full cover: no read-back
        continue;
      }
      auto cur = emit_field_read_chain(mem_name, idx, f.name);
      emit_field_store(mem_name, idx, f.name, splice_const(cur, rel, ov_bits, piece));
    }
    return true;
  }

  auto store = [&](const std::string& value) {
    auto st = builder_.add_child(Lnast_ntype::create_store());
    ln.add_child(st, Lnast_node::create_ref(mem_name));
    builder_.add_value_child_pub(st, idx);
    builder_.add_value_child_pub(st, value);
  };
  // The entry's current word, the read-back of a splice.
  auto read_entry = [&]() {
    auto tg  = builder_.add_child(Lnast_ntype::create_tuple_get());
    auto cur = builder_.create_lnast_tmp();
    ln.add_child(tg, Lnast_node::create_ref(cur));
    ln.add_child(tg, Lnast_node::create_ref(mem_name));
    builder_.add_value_child_pub(tg, idx);
    builder_.note_unsigned_bits(cur, mi.elem_bits);
    return to_pattern(cur, mi.elem_bits, false);
  };

  // A constant slice covering the whole element word is a plain element store —
  // no read-back, which is what a single-write-port `arr[r][c][0]` unrolls to.
  if (lo_bit) {
    store(width == mi.elem_bits ? val : trunc_to(splice_const(read_entry(), *lo_bit, width, val), mi.elem_bits));
    return true;
  }

  // A runtime position is an inclusive `range(lo, hi)` mask: the same chain as
  // the constant one, so it merges just the same.
  auto range_splice = [&](const std::string& lo, const std::string& hi, const std::string& piece) {
    store(trunc_to(splice_range(read_entry(), lo, hi, piece), mi.elem_bits));
  };
  // Verilog writes only the bits of the part select inside the word (see
  // clip_window): the emitted range is inside the word by construction.
  const auto  win = clip_window(dyn_lo, width, mi.elem_bits, val, lo_bounds);
  If_in_range meets(builder_, win.meets);
  range_splice(win.lo, win.hi, win.piece);
  return true;
}

// Unpacked-array PORT element access: the port is a FLAT packed bus, so
// element `arr[idx]` is a declaration-order slice: the rightmost element is in
// the packed bus LSB. This is `(idx-lower)*elem_bits` for descending ranges and
// `(upper-idx)*elem_bits` for ascending ranges. It is NOT a memory
// store/tuple_get.
std::string Slang_context::flat_port_read(const slang::ast::ElementSelectExpression& es, const Mem_info& mi) {
  const auto* base_sym  = resolve_base_symbol(es.value());
  const int   flat_bits = mi.elem_bits * static_cast<int>(mi.size);
  auto        p         = to_pattern(to_int_value(read_symbol(*base_sym, es.value().sourceRange)), flat_bits, false);

  if (auto ci = try_eval_int(es.selector())) {
    int64_t lo_bit = (mi.descending ? *ci - mi.lower : mi.upper - *ci) * mi.elem_bits;
    if (lo_bit < 0 || lo_bit + mi.elem_bits > flat_bits) {
      emit_warning(es.sourceRange, "select-out-of-range", "bitwidth", "constant array-port select is out of range");
      lo_bit = std::max<int64_t>(lo_bit, 0);
    }
    auto r = extract_field(p, lo_bit, mi.elem_bits);
    return mi.elem_signed ? builder_.create_sext_stmts(r, std::to_string(mi.elem_bits - 1)) : r;
  }

  auto idx = to_int_value(lower_rvalue(es.selector()));
  if (mi.descending) {
    if (mi.lower != 0) {
      idx = builder_.create_minus_stmts(idx, std::to_string(mi.lower));
    }
  } else {
    idx = builder_.create_minus_stmts(std::to_string(mi.upper), idx);
  }
  std::string shamt    = mi.elem_bits != 1 ? builder_.create_mult_stmts(idx, std::to_string(mi.elem_bits)) : idx;
  const int   bias     = mi.elem_bits;
  // Widen the shift amount so `+ bias` cannot drop its carry when cgen inlines it
  // into the Verilog shift operand (self-determined width = max(operand widths)).
  // Mirrors lower_select in slang_expr.cpp: a clog2(size)-bit idx makes `idx + 1`
  // wrap to 0 at the top index, silently returning the wrong element. Hold
  // flat_bits + bias, plus one headroom bit for the carry.
  int         amt_bits = 0;
  for (int t = flat_bits + bias; t > 0; t >>= 1) {
    ++amt_bits;
  }
  ++amt_bits;
  shamt        = builder_.create_plus_stmts(trunc_to(shamt, amt_bits), std::to_string(bias));
  auto shifted = builder_.create_sra_stmts(builder_.create_shl_stmts(p, std::to_string(bias)), shamt);
  auto r       = trunc_to(shifted, mi.elem_bits);
  return mi.elem_signed ? builder_.create_sext_stmts(r, std::to_string(mi.elem_bits - 1)) : r;
}

void Slang_context::flat_port_write(const slang::ast::ElementSelectExpression& es, const Mem_info& mi, const std::string& rhs) {
  const auto* base_sym  = resolve_base_symbol(es.value());
  const int   flat_bits = mi.elem_bits * static_cast<int>(mi.size);
  auto        base_name = write_target_of(*base_sym);
  auto        val       = to_pattern(to_int_value(rhs), mi.elem_bits, mi.elem_signed);

  if (auto ci = try_eval_int(es.selector())) {
    int64_t lo_bit = (mi.descending ? *ci - mi.lower : mi.upper - *ci) * mi.elem_bits;
    if (lo_bit < 0 || lo_bit + mi.elem_bits > flat_bits) {
      emit_warning(es.sourceRange, "select-out-of-range", "bitwidth", "constant array-port select is out of range");
      lo_bit = std::max<int64_t>(lo_bit, 0);
    }
    note_write(*base_sym, current_assign_nonblocking_, es.sourceRange.start());
    builder_.create_set_mask_stmts(base_name, val, std::to_string(lo_bit), std::to_string(lo_bit + mi.elem_bits));
    return;
  }

  // Dynamic element index: a range write of the element's lane, blocking or
  // not. An index past the array writes nothing.
  const auto& sel    = es.selector();
  auto        idx    = to_int_value(lower_rvalue(sel));
  auto        bounds = selector_bounds(sel);
  if (mi.descending) {
    if (mi.lower != 0) {
      idx = builder_.create_minus_stmts(idx, std::to_string(mi.lower));
    }
    bounds = affine_bounds(bounds, -mi.lower, 1);
  } else {
    idx    = builder_.create_minus_stmts(std::to_string(mi.upper), idx);
    bounds = affine_bounds(bounds, mi.upper, -1);
  }
  std::string in_range;
  idx     = guard_element_index(idx, bounds, mi.size, /*at_root=*/true, in_range);
  auto lo = mi.elem_bits != 1 ? builder_.create_mult_stmts(idx, std::to_string(mi.elem_bits)) : idx;
  auto hi = mi.elem_bits != 1 ? builder_.create_plus_stmts(lo, std::to_string(mi.elem_bits - 1)) : lo;
  note_write(*base_sym, current_assign_nonblocking_, es.sourceRange.start());
  If_in_range guard(builder_, in_range);
  emit_dynamic_slice_write(base_name, lo, hi, val);
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

bool Slang_context::resolve_packed_lvalue(const slang::ast::Expression& lhs, Packed_lv& out, bool static_only) {
  using slang::ast::RangeSelectionKind;

  switch (lhs.kind) {
    case ExpressionKind::NamedValue       :
    case ExpressionKind::HierarchicalValue: {
      const auto& sym = lhs.as<slang::ast::ValueExpressionBase>().symbol;
      const auto& ct  = sym.getType().getCanonicalType();
      if (!ct.isIntegral() || !ct.hasFixedRange()) {
        return false;  // unpacked / non-packed root: not a packed slice
      }
      if (!static_only && !declared_.contains(&sym) && !input_syms_.contains(&sym)) {
        declare_value_symbol(sym, /*force_reg=*/false);
      }
      auto ti       = tinfo(sym.getType());
      out.base      = &sym;
      out.const_off = 0;
      out.dyn_off.clear();
      out.in_range.clear();
      out.window.reset();
      out.width     = ti.bits;
      out.is_signed = ti.is_signed;
      out.reach_lo  = 0;
      out.reach_hi  = ti.bits - 1;
      return true;
    }

    case ExpressionKind::Conversion:
      // a same-bitstream-width bitcast passes through to the inner target
      return resolve_packed_lvalue(lhs.as<slang::ast::ConversionExpression>().operand(), out, static_only);

    case ExpressionKind::MemberAccess: {
      const auto& ma = lhs.as<slang::ast::MemberAccessExpression>();
      if (ma.member.kind != slang::ast::SymbolKind::Field || !ma.value().type->isIntegral()) {
        return false;
      }
      // A member of a clipped part select has no single position.
      if (!resolve_packed_lvalue(ma.value(), out, static_only) || out.window) {
        return false;
      }
      const auto& field  = ma.member.as<slang::ast::FieldSymbol>();
      auto        ti     = tinfo(*lhs.type);
      out.const_off     += static_cast<int64_t>(field.bitOffset);  // field offset within its struct
      out.width          = ti.bits;
      out.is_signed      = ti.is_signed;
      if (out.dyn_off.empty()) {
        out.reach_lo = std::max(out.reach_lo, out.const_off);
        out.reach_hi = std::min(out.reach_hi, out.const_off + ti.bits - 1);
      }
      return true;
    }

    case ExpressionKind::ElementSelect:
    case ExpressionKind::RangeSelect  : {
      const auto& base    = lhs.kind == ExpressionKind::ElementSelect ? lhs.as<slang::ast::ElementSelectExpression>().value()
                                                                      : lhs.as<slang::ast::RangeSelectExpression>().value();
      const auto& base_ty = base.type->getCanonicalType();

      // Flattened array element: `arr[idx]` is a bit-slice of the packed bus
      // var. The element-select base is the unpacked array, so the generic
      // packed recursion (which needs a packed base) does not apply — resolve
      // it directly to the bus root plus the element bit-offset. A `.field` /
      // `[slice]` wrapper above then folds in via the MemberAccess/packed cases.
      if (lhs.kind == ExpressionKind::ElementSelect && base_ty.isUnpackedArray()) {
        if (static_only) {
          return false;
        }
        const auto* fsym = resolve_base_symbol(base);
        if (fsym != nullptr && flat_port_syms_.contains(fsym)) {
          const auto& mi = mem_info_.at(fsym);
          out.base       = fsym;
          out.const_off  = 0;
          out.dyn_off.clear();
          out.in_range.clear();
          out.window.reset();
          out.width       = mi.elem_bits;
          out.is_signed   = mi.elem_signed;
          out.reach_lo    = 0;
          out.reach_hi    = mi.size * mi.elem_bits - 1;
          const auto& sel = lhs.as<slang::ast::ElementSelectExpression>().selector();
          if (auto ci = try_eval_int(sel)) {
            out.const_off = (mi.descending ? *ci - mi.lower : mi.upper - *ci) * mi.elem_bits;
            out.reach_lo  = out.const_off;
            out.reach_hi  = out.const_off + mi.elem_bits - 1;
          } else {
            auto idx    = to_int_value(lower_rvalue(sel));
            auto bounds = selector_bounds(sel);
            if (mi.descending) {
              if (mi.lower != 0) {
                idx = builder_.create_minus_stmts(idx, std::to_string(mi.lower));
              }
              bounds = affine_bounds(bounds, -mi.lower, 1);
            } else {
              idx    = builder_.create_minus_stmts(std::to_string(mi.upper), idx);
              bounds = affine_bounds(bounds, mi.upper, -1);
            }
            idx         = guard_element_index(idx, bounds, mi.size, /*at_root=*/true, out.in_range);
            out.dyn_off = mi.elem_bits != 1 ? builder_.create_mult_stmts(idx, std::to_string(mi.elem_bits)) : idx;
          }
          return true;
        }
      }

      if (!base_ty.isIntegral() || !base_ty.hasFixedRange()) {
        return false;  // unpacked array element / non-packed base
      }
      if (static_only) {
        if (lhs.kind == ExpressionKind::ElementSelect) {
          if (!try_eval_int(lhs.as<slang::ast::ElementSelectExpression>().selector())) {
            return false;
          }
        } else {
          const auto& rs = lhs.as<slang::ast::RangeSelectExpression>();
          if (!try_eval_int(rs.left()) || !try_eval_int(rs.right())) {
            return false;
          }
        }
      }
      // A select of a clipped part select has no single position.
      if (!resolve_packed_lvalue(base, out, static_only) || out.window) {
        return false;
      }

      auto range  = base_ty.getFixedRange();
      int  stride = base_ty.isPackedArray() ? static_cast<int>(base_ty.getArrayElementType()->getBitWidth()) : 1;
      auto ti     = tinfo(*lhs.type);

      // local low-bit offset within `base`, in element units (const and/or
      // dynamic), and the static range of a dynamic one
      std::optional<int64_t>                     const_low;
      std::string                                dyn_low;
      std::optional<std::pair<int64_t, int64_t>> low_bounds;
      auto normalize = [&](const slang::ast::Expression& idx, int64_t width_down, int64_t width_up) {
        if (auto ci = try_eval_int(idx)) {
          int64_t bottom = range.isDescending() ? (*ci - range.lower() - (width_down - 1)) : (range.upper() - *ci - (width_up - 1));
          const_low      = bottom;
          return;
        }
        auto v = to_int_value(lower_rvalue(idx));  // settle the selector to an int (match the rvalue select path)
        if (range.isDescending()) {
          int64_t bias = range.lower() + (width_down - 1);
          dyn_low      = bias == 0 ? v : builder_.create_minus_stmts(v, std::to_string(bias));
          low_bounds   = affine_bounds(selector_bounds(idx), -bias, 1);
          return;
        }
        int64_t bias = range.upper() - (width_up - 1);
        dyn_low      = builder_.create_minus_stmts(std::to_string(bias), v);
        low_bounds   = affine_bounds(selector_bounds(idx), bias, -1);
      };

      // `base` is the root variable itself (not a member or an element of it)
      const auto* root = &base;
      while (root->kind == ExpressionKind::Conversion) {
        root = &root->as<slang::ast::ConversionExpression>().operand();
      }
      const bool    whole_root = root->kind == ExpressionKind::NamedValue || root->kind == ExpressionKind::HierarchicalValue;
      const int64_t count      = static_cast<int64_t>(range.width());  // elements of `base`
      if (lhs.kind == ExpressionKind::ElementSelect) {
        normalize(lhs.as<slang::ast::ElementSelectExpression>().selector(), 1, 1);
        if (!dyn_low.empty()) {
          // an index past `base` writes nothing
          dyn_low = guard_element_index(dyn_low, low_bounds, count, whole_root, out.in_range);
        }
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
            normalize(rs.left(), 1, w);
          } else {
            normalize(rs.left(), w, 1);
          }
        }
      }

      // fold the local offset (bits) into the accumulator
      if (const_low) {
        out.const_off += (*const_low) * stride;
      }
      if (!dyn_low.empty()) {
        std::string term            = stride == 1 ? dyn_low : builder_.create_mult_stmts(dyn_low, std::to_string(stride));
        // A runtime part select that may leave `base` keeps the container, so
        // the writer can clip the window to it. When `base` is the root
        // itself, only a window that can start below bit 0 needs that: the
        // write already stops at the root's declared width.
        const auto  term_bounds     = scaled_bounds(low_bounds, stride);
        const bool  may_be_negative = !term_bounds || term_bounds->first < 0;
        const bool  may_pass_top    = !term_bounds || term_bounds->second + ti.bits > count * stride;
        if (lhs.kind == ExpressionKind::RangeSelect && (may_be_negative || (may_pass_top && !whole_root))) {
          out.window = Packed_window{.cont_const = out.const_off,
                                     .cont_dyn   = out.dyn_off,
                                     .cont_bits  = count * stride,
                                     .clip_top   = !whole_root,
                                     .lo         = term,
                                     .lo_bounds  = term_bounds};
        }
        out.dyn_off = out.dyn_off.empty() ? term : builder_.create_plus_stmts(out.dyn_off, term);
      }
      out.width     = ti.bits;
      out.is_signed = ti.is_signed;
      if (out.dyn_off.empty()) {
        out.reach_lo = std::max(out.reach_lo, out.const_off);
        out.reach_hi = std::min(out.reach_hi, out.const_off + ti.bits - 1);
      }
      return true;
    }

    default: return false;
  }
}

void Slang_context::emit_packed_rmw(const Packed_lv& lv, const std::string& rhs, slang::SourceRange sr) {
  if (auto it = packed_wire_bits_.find(lv.base); it != packed_wire_bits_.end()) {
    if (!lv.dyn_off.empty() || lv.const_off < 0 || lv.const_off + lv.width > static_cast<int64_t>(it->second.size())) {
      emit_unsupported(sr, "unsupported-packed-wire-write", "concurrent packed wires require constant in-range destinations");
      return;
    }
    for (int64_t bit = 0; bit < lv.width; ++bit) {
      builder_.create_assign_stmts(it->second[lv.const_off + bit], extract_field(rhs, bit, 1));
    }
    return;
  }
  if (auto it = bit_regs_.find(lv.base); it != bit_regs_.end()) {
    if (!current_assign_nonblocking_ || proc_kind_ != Proc_kind::seq || !lv.dyn_off.empty() || lv.const_off < 0
        || lv.const_off + lv.width > static_cast<int64_t>(it->second.size())) {
      emit_unsupported(sr,
                       "unsupported-bit-clock-write",
                       "independently clocked vector bits require constant nonblocking destinations");
      return;
    }
    note_write(*lv.base, true, sr.start());
    for (int64_t bit = 0; bit < lv.width; ++bit) {
      const auto& target = it->second[lv.const_off + bit];
      builder_.create_assign_stmts(target, extract_field(rhs, bit, 1));
      proc_bit_reg_writes_.insert(target);
    }
    return;
  }
  // M7: a partial write whose resolved root is a BUNDLE port, or a per-field
  // struct var, has no flat net to set_mask — split/splice on the field
  // leaves instead. Every packed lvalue chain on such a root (`resp.f = v`,
  // `resp.f[3:0] = v`, `resp[10:3] = v`, `s.arr[j][1:0] = v`, dynamic-index
  // forms) that assign_to's field shortcuts do not take funnels through here.
  if (bundle_port_of(*lv.base) != nullptr || struct_var_info_.contains(lv.base)) {
    emit_leaf_split_rmw(lv, rhs, sr);
    return;
  }

  // value written into the slice comes from the LOW bits of the RHS
  // A SystemVerilog assignment to a packed field/slice first fits the RHS to
  // the selected width. Make that precision boundary explicit here: LNAST
  // integers are otherwise unbounded, so an arithmetic RHS can reach the
  // bitwidth verifier one carry bit wider than the destination slice.
  auto val       = trunc_to(rhs, static_cast<int>(lv.width));
  // The net this RMW modifies. For a partially-registered var an edge-process
  // write lands on the FLOP, not on the combinational composite the symbol's
  // own name now denotes -- and it must also READ the flop here, because the
  // unmasked bits are the ones being HELD and because sibling generate-block
  // drivers chain their disjoint slices through this same name (reading the
  // composite instead made every driver seed from the same value, so the last
  // one to emit silently discarded the others). Value reads inside `rhs` went
  // through lname_of and already see the composite, which is the whole point.
  auto base_name = write_target_of(*lv.base);

  if (lv.dyn_off.empty()) {
    int64_t lo_bit = lv.const_off;
    if (lo_bit < 0) {
      emit_warning(sr, "select-out-of-range", "bitwidth", "constant select is out of the declared range");
      lo_bit = 0;
    }
    note_write(*lv.base, current_assign_nonblocking_, sr.start());
    builder_.create_set_mask_stmts(base_name, val, std::to_string(lo_bit), std::to_string(lo_bit + lv.width));
    return;
  }

  // A runtime position is a range write, blocking or nonblocking: pending
  // writes supply the untouched bits, and RHS/index reads still observe Q.
  const auto dr = dynamic_write_range(lv, val);
  note_write(*lv.base, current_assign_nonblocking_, sr.start());
  const auto bi = tinfo(lv.base->getType());
  if (!current_assign_nonblocking_ && bi.is_signed && !flat_port_syms_.contains(lv.base)) {
    // A signed local declares no range (declare_value_symbol) and every store
    // to it is fit_wrap'd, so the write splices a copy of its bit pattern and
    // stores the sign-extended word back.
    auto        cur_p = to_pattern(read_symbol(*lv.base, sr), bi.bits, true);
    If_in_range lands(builder_, dr.in_range);
    builder_.create_assign_stmts(base_name, fit_wrap(splice_range(cur_p, dr.lo, dr.hi, dr.piece), bi.bits, true));
    return;
  }
  If_in_range lands(builder_, dr.in_range);
  emit_dynamic_slice_write(base_name, dr.lo, dr.hi, dr.piece);
}

std::string Slang_context::splice_range(const std::string& src, const std::string& lo, const std::string& hi,
                                        const std::string& piece) {
  auto  upper = builder_.create_plus_stmts(hi, "1");
  auto& ln    = *builder_.lnast;
  auto  dst   = builder_.create_lnast_tmp();
  auto  sm    = builder_.add_child(Lnast_ntype::create_set_mask());
  ln.add_child(sm, Lnast_node::create_ref(dst));
  builder_.add_value_child_pub(sm, src);
  builder_.add_value_child_pub(sm, piece);
  builder_.add_value_child_pub(sm, lo);
  builder_.add_value_child_pub(sm, upper);
  return dst;
}

Slang_context::Dynamic_range Slang_context::dynamic_write_range(const Packed_lv& lv, const std::string& val) {
  const auto offset = [&](int64_t c, const std::string& d) {
    if (d.empty()) {
      return std::to_string(c);
    }
    return c == 0 ? d : builder_.create_plus_stmts(d, std::to_string(c));
  };
  const auto width = static_cast<int>(lv.width);
  if (!lv.window) {
    auto lo = offset(lv.const_off, lv.dyn_off);
    auto hi = width == 1 ? lo : builder_.create_plus_stmts(lo, std::to_string(width - 1));
    return {.lo = lo, .hi = hi, .piece = val, .in_range = lv.in_range};
  }
  const auto& w    = *lv.window;
  const auto  win  = clip_window(w.lo, width, w.cont_bits, val, w.lo_bounds, w.clip_top);
  const auto  cont = offset(w.cont_const, w.cont_dyn);
  auto        in   = lv.in_range;
  if (!win.meets.empty()) {
    in = in.empty() ? win.meets : builder_.create_log_and_stmts(in, win.meets);
  }
  if (cont == "0") {
    return {.lo = win.lo, .hi = win.hi, .piece = win.piece, .in_range = in};
  }
  auto lo = builder_.create_plus_stmts(cont, win.lo);
  auto hi = win.hi == win.lo ? lo : builder_.create_plus_stmts(cont, win.hi);
  return {.lo = lo, .hi = hi, .piece = win.piece, .in_range = in};
}

Slang_context::Clipped_window Slang_context::clip_window(const std::string& lo, int width, int64_t cont_bits,
                                                         const std::string&                                val,
                                                         const std::optional<std::pair<int64_t, int64_t>>& lo_bounds,
                                                         bool                                              clip_top) {
  auto       hi              = width == 1 ? lo : builder_.create_plus_stmts(lo, std::to_string(width - 1));
  const auto top             = cont_bits - 1;
  const bool may_be_negative = !lo_bounds || lo_bounds->first < 0;
  const bool may_pass_top    = clip_top ? !lo_bounds || lo_bounds->second + width - 1 > top : !lo_bounds || lo_bounds->second > top;
  if (!may_be_negative && !may_pass_top) {
    return {.meets = "", .lo = lo, .hi = hi, .piece = val};
  }
  // Verilog writes only the bits of a part select inside its container: a
  // window that misses it writes nothing, one running past bit W-1 drops its
  // top bits, and one starting below bit 0 (`m[i][b -: 4]` with b < 3) drops
  // the value's low `-lo` bits. Spell that out: the write is guarded by the
  // window meeting the container, and its bounds are clipped to narrow typed
  // values, so the range is inside the container by construction (Pyrope,
  // which the writer emits, has no out-of-range write). Without `clip_top`
  // only `lo` is kept inside: the destination's width drops the rest.
  const int pos_bits = std::max(1, static_cast<int>(std::bit_width(static_cast<uint64_t>(top))));
  const int hi_bits  = clip_top ? pos_bits : std::max(1, static_cast<int>(std::bit_width(static_cast<uint64_t>(top + width - 1))));

  // `mut t:u<bits> = seed; if cond { t = alt }`: a typed mux temp, the
  // lower_conditional_expr shape.
  auto clip = [&](const std::string& seed, int bits, const std::string& cond, const std::string& alt) {
    auto t = fresh_local("clip");
    builder_.create_declare_stmts(t, "mut", int_max_str(bits, false), int_min_str(bits, false));
    builder_.create_assign_stmts(t, seed);
    auto guard = builder_.create_if_stmt(false);
    builder_.add_if_cond(guard, cond);
    builder_.push_stmts(builder_.add_if_stmts(guard));
    builder_.create_assign_stmts(t, alt);
    builder_.pop_stmts();
    return t;
  };
  std::string meets;  // the window meets the container (only the checks the bounds leave open)
  if (!lo_bounds || lo_bounds->first + width - 1 < 0) {
    meets = builder_.create_ge_stmts(hi, "0");
  }
  if (!lo_bounds || lo_bounds->second > top) {
    auto below_top = builder_.create_le_stmts(lo, std::to_string(top));
    meets          = meets.empty() ? below_top : builder_.create_log_and_stmts(meets, below_top);
  }
  auto lo_c  = trunc_to(lo, pos_bits);
  auto hi_c  = width == 1 ? lo_c : trunc_to(hi, hi_bits);
  auto piece = val;
  if (width > 1 && may_be_negative) {
    // -lo is in [1, width - 1] when the window starts below bit 0 and meets
    // the container: keep just those bits, so the shift amount is non-negative.
    auto below = builder_.create_lt_stmts(lo, "0");
    auto drop  = trunc_to(builder_.create_minus_stmts("0", lo), std::max(1, std::bit_width(static_cast<unsigned>(width - 1))));
    lo_c       = clip(lo_c, pos_bits, below, "0");
    piece      = clip(val, width, below, builder_.create_sra_stmts(val, drop));
  }
  if (width > 1 && may_pass_top && clip_top) {
    hi_c = clip(hi_c, pos_bits, builder_.create_gt_stmts(hi, std::to_string(top)), std::to_string(top));
  }
  return {.meets = meets, .lo = lo_c, .hi = hi_c, .piece = piece};
}

std::string Slang_context::guard_element_index(const std::string& idx, const std::optional<std::pair<int64_t, int64_t>>& bounds,
                                               int64_t count, bool at_root, std::string& in_range) {
  const bool may_be_negative = !bounds || bounds->first < 0;
  const bool may_pass_top    = !bounds || bounds->second > count - 1;
  if (!may_be_negative && (!may_pass_top || at_root)) {
    return idx;
  }
  std::string ok;
  if (may_be_negative) {
    ok = builder_.create_ge_stmts(idx, "0");
  }
  if (may_pass_top) {
    auto le = builder_.create_le_stmts(idx, std::to_string(count - 1));
    ok      = ok.empty() ? le : builder_.create_log_and_stmts(ok, le);
  }
  in_range = in_range.empty() ? ok : builder_.create_log_and_stmts(in_range, ok);
  // Under the check the ordinal fits its own width, so the position computed
  // from it is non-negative by construction.
  return trunc_to(idx, std::max(1, static_cast<int>(std::bit_width(static_cast<uint64_t>(count - 1)))));
}

void Slang_context::emit_dynamic_slice_write(const std::string& base, const std::string& lo, const std::string& hi,
                                             const std::string& value) {
  builder_.create_set_mask_stmts(base, value, lo, builder_.create_plus_stmts(hi, "1"));
}
