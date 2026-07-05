//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Module / process / instance lowering (todo/ 2s subtasks A+C). Two-phase
// like CIRCT Structure.cpp: each module body is collected (state-variable
// classification seeded from always-block timing patterns, the yosys-slang
// async_pattern.cc model) before its members lower in source order. Every
// module emits one Lnast in the extracted unit form; instances lower to
// func_call statements the upass/tolg Sub machinery resolves by module name.

#include <algorithm>
#include <cctype>
#include <functional>
#include <vector>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Statement.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang_context.hpp"

using slang::ast::ExpressionKind;
using slang::ast::StatementKind;
using slang::ast::SymbolKind;

namespace {

// True iff `type` bottoms out in plain bits with no STRUCT anywhere in its
// shape (recurses through any number of packed/unpacked array dimensions).
// Used to gate the per-field leaf-wire bundle split: a field that is a plain
// multi-dimensional array (e.g. CIRCT's `logic [1:0][5:0] enq`) is fine — its
// whole value is just `getBitWidth()` flat bits, same as a scalar field — but
// a (possibly array-of-) nested struct field needs its own recursive split,
// which the bundle path does not do (see is_scalar_struct_var).
bool field_type_is_struct_free(const slang::ast::Type& type) {
  const auto& ct = type.getCanonicalType();
  if (ct.isStruct()) {
    return false;
  }
  if (ct.isPackedArray() || ct.isUnpackedArray()) {
    return field_type_is_struct_free(*ct.getArrayElementType());
  }
  return true;
}

// True iff a field of this type CANNOT be represented as an independent bundle
// leaf without breaking the deep-access routing. A plain (non-array) nested
// packed struct is fine: a deep read `io.sub.x` lowers as a MemberAccess whose
// base `io.sub` routes through the leaf net (read_leaf), and a whole-copy /
// whole-read reassembles the leaf as flat bits — so `sub` gets its own leaf and
// the false self-loop is broken (small_todo_working.md Type B). But ANY ARRAY
// dimension over a non-struct-free element forces the flat bus: a deep access
// `io.arr[i]` lowers as an ElementSelect on the leaf, which does not route back
// to the leaf net (it needs the whole-struct bus + element bit-offset), so the
// struct must stay flat. Recurses so a nested struct that itself holds an
// array-of-struct field also forces the flat bus.
bool field_forces_flat_bus(const slang::ast::Type& type) {
  const auto& ct = type.getCanonicalType();
  if (ct.isPackedArray() || ct.isUnpackedArray()) {
    return !field_type_is_struct_free(type);  // array-of-struct (or struct-through-array)
  }
  if (ct.isStruct()) {
    for (const auto& f : ct.as<slang::ast::PackedStructType>().membersOfType<slang::ast::FieldSymbol>()) {
      if (field_forces_flat_bus(f.getType())) {
        return true;
      }
    }
    return false;  // nested struct whose sub-fields are all bundle-safe
  }
  return false;  // plain bits
}

// Walk an assignment LHS spine down to the written base symbol.
const slang::ast::ValueSymbol* lhs_base_symbol(const slang::ast::Expression& lhs) {
  const auto* e = &lhs;
  while (true) {
    switch (e->kind) {
      case ExpressionKind::NamedValue:
      case ExpressionKind::HierarchicalValue: return &e->as<slang::ast::ValueExpressionBase>().symbol;
      case ExpressionKind::ElementSelect: e = &e->as<slang::ast::ElementSelectExpression>().value(); break;
      case ExpressionKind::RangeSelect: e = &e->as<slang::ast::RangeSelectExpression>().value(); break;
      case ExpressionKind::MemberAccess: e = &e->as<slang::ast::MemberAccessExpression>().value(); break;
      case ExpressionKind::Conversion: e = &e->as<slang::ast::ConversionExpression>().operand(); break;
      case ExpressionKind::Concatenation: return nullptr;  // caller iterates operands itself
      default: return nullptr;
    }
  }
}

// Collect the symbols written by a statement subtree, split by style.
struct Write_collector : public slang::ast::ASTVisitor<Write_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*> blocking;
  absl::flat_hash_set<const slang::ast::ValueSymbol*> nonblocking;

  void handle(const slang::ast::AssignmentExpression& expr) {
    auto& set = expr.isNonBlocking() ? nonblocking : blocking;

    std::function<void(const slang::ast::Expression&)> note = [&](const slang::ast::Expression& lhs) {
      if (lhs.kind == ExpressionKind::Concatenation) {
        for (const auto* op : lhs.as<slang::ast::ConcatenationExpression>().operands()) {
          note(*op);
        }
        return;
      }
      if (const auto* sym = lhs_base_symbol(lhs)) {
        set.insert(sym);
      }
    };
    note(expr.left());

    visitDefault(expr);
  }
};

// Collects every unpacked-array element-select in the module along with its
// selector, plus the set of for-loop INDUCTION variables, so the caller can
// classify each array as constant- or runtime-indexed (a selector that folds,
// or references only loop-induction vars / params / genvars, is constant after
// the reader unrolls). Recurses through generate instances.
struct Array_index_collector : public slang::ast::ASTVisitor<Array_index_collector, slang::ast::VisitFlags::AllGood> {
  std::vector<std::pair<const slang::ast::ValueSymbol*, const slang::ast::Expression*>> selects;
  // Element-selects whose base is a PACKED array with a >1-bit element (a true
  // `[N][W]`, W>1) — a candidate register file. Classified the same way as
  // unpacked selects; a runtime selector marks the base for memory-ization.
  std::vector<std::pair<const slang::ast::ValueSymbol*, const slang::ast::Expression*>> packed_selects;
  absl::flat_hash_set<const slang::ast::Symbol*>                                        loop_vars;

  void handle(const slang::ast::ForLoopStatement& f) {
    for (const auto* lv : f.loopVars) {  // `for (int i = ...)` header-declared counters
      loop_vars.insert(lv);
    }
    for (const auto* ie : f.initializers) {  // `for (i = ...)` counters declared outside the header
      if (ie->kind == ExpressionKind::Assignment) {
        const auto& a = ie->as<slang::ast::AssignmentExpression>();
        if (a.left().kind == ExpressionKind::NamedValue) {
          loop_vars.insert(&a.left().as<slang::ast::NamedValueExpression>().symbol);
        }
      }
    }
    visitDefault(f);
  }

  void handle(const slang::ast::ElementSelectExpression& es) {
    const auto& bct = es.value().type->getCanonicalType();
    if (bct.isUnpackedArray()) {
      if (const auto* sym = lhs_base_symbol(es.value())) {
        selects.emplace_back(sym, &es.selector());
      }
    } else if (bct.isPackedArray() && bct.kind == slang::ast::SymbolKind::PackedArrayType
               && bct.as<slang::ast::PackedArrayType>().elementType.getCanonicalType().getBitWidth() > 1) {
      // Element-select of a packed 2-D array (`[N][W]`, W>1): an element access,
      // not a bit-select. A runtime selector makes this a memory candidate.
      if (const auto* sym = lhs_base_symbol(es.value())) {
        packed_selects.emplace_back(sym, &es.selector());
      }
    }
    visitDefault(es);
  }
};

// True iff every symbol the selector references is statically resolvable once
// the reader unrolls loops: a for-loop induction var, a genvar, a parameter, or
// an enum value. A reference to any genuine runtime signal (net / port /
// register / module variable) makes the index runtime — keep that array a
// memory (dynamic-shift flattening mismatches; cf. the `tuplish` regression).
struct Static_selector_scan : public slang::ast::ASTVisitor<Static_selector_scan, slang::ast::VisitFlags::AllGood> {
  const absl::flat_hash_set<const slang::ast::Symbol*>* loop_vars = nullptr;
  bool                                                  all_static = true;

  void handle(const slang::ast::ValueExpressionBase& e) {
    const auto k = e.symbol.kind;
    if (k != slang::ast::SymbolKind::Genvar && k != slang::ast::SymbolKind::Parameter
        && k != slang::ast::SymbolKind::EnumValue && !loop_vars->contains(&e.symbol)) {
      all_static = false;
    }
  }
};

// Collect every NamedValue leaf symbol referenced by an expression subtree.
struct Named_value_collector : public slang::ast::ASTVisitor<Named_value_collector, slang::ast::VisitFlags::AllGood> {
  std::vector<const slang::ast::ValueSymbol*> syms;
  void handle(const slang::ast::NamedValueExpression& e) {
    syms.push_back(&e.symbol);
    visitDefault(e);
  }
};

// Collect the base symbol of every member-access (`x.field`) in the module, so
// the caller knows which struct nets are read by-field (vs only whole).
struct Member_read_collector : public slang::ast::ASTVisitor<Member_read_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*>* out = nullptr;
  void handle(const slang::ast::MemberAccessExpression& ma) {
    if (const auto* sym = lhs_base_symbol(ma.value())) {
      out->insert(sym);
    }
    visitDefault(ma);
  }
};

// Flags a packed-struct VAR accessed BELOW its top level (`c0.field[i]`,
// `c0.field.sub`): the per-field bundle path only resolves single-level
// `c0.field`, so a deeper access roots at a FLAT bus and collides with the bundle
// leaves. Such vars must stay a flat bus.
struct Deep_struct_access_collector
    : public slang::ast::ASTVisitor<Deep_struct_access_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*>* out = nullptr;
  void note(const slang::ast::Expression& base) {
    if (base.kind == slang::ast::ExpressionKind::MemberAccess) {
      if (const auto* sym = lhs_base_symbol(base.as<slang::ast::MemberAccessExpression>().value())) {
        out->insert(sym);
      }
    }
  }
  void handle(const slang::ast::ElementSelectExpression& e) {
    note(e.value());
    visitDefault(e);
  }
  void handle(const slang::ast::RangeSelectExpression& e) {
    note(e.value());
    visitDefault(e);
  }
  void handle(const slang::ast::MemberAccessExpression& ma) {
    note(ma.value());
    visitDefault(ma);
  }
};

// Flags a packed-struct VAR deep-WRITTEN (`io.sub.x = v`, `io.field[i] = v`):
// the per-field bundle path has no whole-struct net for resolve_packed_lvalue to
// root the read-modify-write on, so a deep-written struct with a nested field
// must stay a flat bus. (A deep READ of a nested-struct field is safe — it
// routes through the leaf net; see field_forces_flat_bus / is_scalar_struct_var.)
struct Deep_struct_write_collector
    : public slang::ast::ASTVisitor<Deep_struct_write_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*>* out = nullptr;
  // `base` is the value() the OUTERMOST lvalue select/member sits on; the write
  // is "deep" when that base is itself a member/element/range select.
  void note_deep(const slang::ast::Expression& base) {
    if (base.kind == slang::ast::ExpressionKind::MemberAccess
        || base.kind == slang::ast::ExpressionKind::ElementSelect
        || base.kind == slang::ast::ExpressionKind::RangeSelect) {
      if (const auto* sym = lhs_base_symbol(base)) {
        out->insert(sym);
      }
    }
  }
  void note_lhs(const slang::ast::Expression& lhs) {
    switch (lhs.kind) {
      case slang::ast::ExpressionKind::MemberAccess: note_deep(lhs.as<slang::ast::MemberAccessExpression>().value()); return;
      case slang::ast::ExpressionKind::ElementSelect: note_deep(lhs.as<slang::ast::ElementSelectExpression>().value()); return;
      case slang::ast::ExpressionKind::RangeSelect: note_deep(lhs.as<slang::ast::RangeSelectExpression>().value()); return;
      case slang::ast::ExpressionKind::Conversion: note_lhs(lhs.as<slang::ast::ConversionExpression>().operand()); return;
      case slang::ast::ExpressionKind::Concatenation:
        for (const auto* op : lhs.as<slang::ast::ConcatenationExpression>().operands()) {
          note_lhs(*op);
        }
        return;
      default: return;
    }
  }
  void handle(const slang::ast::AssignmentExpression& a) {
    note_lhs(a.left());
    visitDefault(a);
  }
};

// Flags a packed-struct VAR that is WHOLE-COPIED (`a = b` as bare names): the
// per-field bundle path detuples the copy per-top-level-field, silently DROPPING
// a field that is a nested struct / array. Flattening keeps the copy intact. Only
// whole-copied structs pay the flat-bus cost (field-access-only structs stay
// bundled).
struct Struct_whole_copy_collector
    : public slang::ast::ASTVisitor<Struct_whole_copy_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*>* out = nullptr;
  void note(const slang::ast::Expression& e) {
    if (e.kind == slang::ast::ExpressionKind::NamedValue) {
      const auto& nv = e.as<slang::ast::NamedValueExpression>();
      if (nv.symbol.getType().getCanonicalType().isStruct()) {
        out->insert(&nv.symbol);
      }
    }
  }
  void handle(const slang::ast::AssignmentExpression& a) {
    // A struct read as a whole (bare RHS name) keeps a flat bus so read_struct_whole
    // reassembles it. Its DESTINATION is a genuine whole-copy only when the RHS is
    // ALSO a bare struct name (`dst = src`) — a pattern assign `io = '{...}` is
    // per-field driven, so its bare-name LHS must NOT be flagged (that false
    // positive kept Type B nested-struct io bundles flat; small_todo_working.md
    // "Type B" — `io` looked whole-copied purely because of its own `'{...}` LHS).
    note(a.right());
    const slang::ast::Expression* r = &a.right();
    while (r->kind == slang::ast::ExpressionKind::Conversion) {
      r = &r->as<slang::ast::ConversionExpression>().operand();
    }
    if (r->kind == slang::ast::ExpressionKind::NamedValue
        && r->as<slang::ast::NamedValueExpression>().symbol.getType().getCanonicalType().isStruct()) {
      note(a.left());  // genuine `dst = src` copy destination
    }
    visitDefault(a);
  }
};

// Flags a plain packed-array LOCAL that is driven by a SINGLE whole PER-ELEMENT
// assignment (a `'{...}` assignment pattern OR a `{...}` bit-concatenation whose
// operands are exactly the array elements) AND read by element select somewhere,
// with NO element/partial writes. This is the CIRCT/firtool shift-network shape
// (small_todo_working.md Type C), e.g. CloseShiftLeftWithMux's
//   assign io_result_res_vec = {{_T_6}, ..., {_T_1}, {io.src}};
// where each `_T_k` reads `io_result_res_vec[k-1]` — a sibling element of the
// same array (often indirectly through a named wire). Lowered to one flat bus the
// sibling read binds to the STALE whole-array bus (poison / a false comb cycle),
// miscompiling / failing to encode. Splitting the array into independent
// per-element leaf nets (declare_array_leaves) routes each sibling read to its own
// net — the array analogue of the per-field struct bundle. Requiring a single
// whole per-element driver and no element writes keeps the split a pure
// representation change (semantically identical) and dodges the dynamic-index-
// write case the leaf path does not handle; ordinary packed arrays are untouched.
struct Array_bundle_collector : public slang::ast::ASTVisitor<Array_bundle_collector, slang::ast::VisitFlags::AllGood> {
  struct Info {
    int  whole_drivers  = 0;
    bool per_elem_driver = false;
    bool elem_written    = false;
    bool elem_read       = false;
    bool nonconst_access = false;  // a dynamic-index / range select — keep flat
  };
  absl::flat_hash_map<const slang::ast::ValueSymbol*, Info>* info = nullptr;

  // A select index is CONSTANT only if it folds to a literal (CIRCT emits `3'h0`
  // etc.). A dynamic index (`arr[sig]`) touches the whole array — there is no
  // per-element independence to exploit, and the flat-bus + dynamic-shift lowering
  // is the correct (and only proven) path — so such an array must stay flat.
  static bool is_dynamic_selector(const slang::ast::Expression& sel) {
    const slang::ast::Expression* s = &sel;
    while (s->kind == slang::ast::ExpressionKind::Conversion) {
      s = &s->as<slang::ast::ConversionExpression>().operand();
    }
    return s->kind != slang::ast::ExpressionKind::IntegerLiteral;
  }

  static bool is_pattern(slang::ast::ExpressionKind k) {
    return k == slang::ast::ExpressionKind::SimpleAssignmentPattern
           || k == slang::ast::ExpressionKind::StructuredAssignmentPattern
           || k == slang::ast::ExpressionKind::ReplicatedAssignmentPattern;
  }
  static bool is_candidate(const slang::ast::ValueSymbol& sym) {
    const auto& ct = sym.getType().getCanonicalType();
    if (!(ct.isPackedArray() && ct.isIntegral() && ct.getArrayElementType() != nullptr)) {
      return false;
    }
    if (!field_type_is_struct_free(*ct.getArrayElementType())) {
      return false;
    }
    // Require a genuine MULTI-BIT element (a real 2-D array like `[6:0][53:0]`),
    // not a plain bus: `logic [1:0]` is a packed array of 1-bit elements too, but
    // splitting a bus into per-bit leaves is wrong (and a self-referencing bus
    // shift already lowers correctly via bit concat, no false cycle).
    return ct.getArrayElementType()->getBitWidth() > 1;
  }
  static int elem_bits_of(const slang::ast::Type& ct) {
    return static_cast<int>(ct.getArrayElementType()->getBitWidth());
  }
  static int elem_count_of(const slang::ast::Type& ct) {
    int eb = elem_bits_of(ct);
    return eb > 0 ? static_cast<int>(ct.getBitWidth()) / eb : 0;
  }

  // Is the RHS a per-element driver of a `count`-element / `elem_bits`-wide array?
  static bool per_element_rhs(const slang::ast::Expression& r, int count, int elem_bits) {
    if (is_pattern(r.kind)) {
      switch (r.kind) {
        case slang::ast::ExpressionKind::SimpleAssignmentPattern:
          return static_cast<int>(r.as<slang::ast::SimpleAssignmentPatternExpression>().elements().size()) == count;
        case slang::ast::ExpressionKind::StructuredAssignmentPattern:
          return static_cast<int>(r.as<slang::ast::StructuredAssignmentPatternExpression>().elements().size()) == count;
        case slang::ast::ExpressionKind::ReplicatedAssignmentPattern:
          return static_cast<int>(r.as<slang::ast::ReplicatedAssignmentPatternExpression>().elements().size()) == count;
        default: return false;
      }
    }
    if (r.kind == slang::ast::ExpressionKind::Concatenation) {
      const auto ops = r.as<slang::ast::ConcatenationExpression>().operands();
      if (static_cast<int>(ops.size()) != count) {
        return false;
      }
      for (const auto* op : ops) {
        if (op->type == nullptr || static_cast<int>(op->type->getBitWidth()) != elem_bits) {
          return false;  // an operand spans more/less than one element
        }
      }
      return true;
    }
    return false;
  }

  void handle(const slang::ast::AssignmentExpression& a) {
    const slang::ast::Expression* l = &a.left();
    while (l->kind == slang::ast::ExpressionKind::Conversion) {
      l = &l->as<slang::ast::ConversionExpression>().operand();
    }
    if (l->kind == slang::ast::ExpressionKind::NamedValue) {
      const auto& sym = l->as<slang::ast::NamedValueExpression>().symbol;
      if (is_candidate(sym)) {
        const auto& ct = sym.getType().getCanonicalType();
        auto&       in = (*info)[&sym];
        in.whole_drivers++;
        const slang::ast::Expression* r = &a.right();
        while (r->kind == slang::ast::ExpressionKind::Conversion) {
          r = &r->as<slang::ast::ConversionExpression>().operand();
        }
        in.per_elem_driver |= per_element_rhs(*r, elem_count_of(ct), elem_bits_of(ct));
      }
    } else if (const auto* base = lhs_base_symbol(*l)) {
      if (is_candidate(*base)) {
        (*info)[base].elem_written = true;  // an element / partial write: keep flat
      }
    }
    visitDefault(a);
  }
  void handle(const slang::ast::ElementSelectExpression& e) {
    if (e.value().kind == slang::ast::ExpressionKind::NamedValue) {
      const auto& sym = e.value().as<slang::ast::NamedValueExpression>().symbol;
      if (is_candidate(sym)) {
        auto& in     = (*info)[&sym];
        in.elem_read = true;
        if (is_dynamic_selector(e.selector())) {
          in.nonconst_access = true;
        }
      }
    }
    visitDefault(e);
  }
  void handle(const slang::ast::RangeSelectExpression& e) {
    // A multi-element range select of the whole array (`arr[hi:lo]`) has no
    // single-element leaf to route to — keep the array flat.
    if (e.value().kind == slang::ast::ExpressionKind::NamedValue) {
      const auto& sym = e.value().as<slang::ast::NamedValueExpression>().symbol;
      if (is_candidate(sym)) {
        (*info)[&sym].nonconst_access = true;
      }
    }
    visitDefault(e);
  }
};

// Single constant-style whole-net driver of a net/var: a `wire x = <expr>`
// initializer or an `assign x = <expr>` (whole net, not a partial select).
// Returns nullptr when none / ambiguous.
const slang::ast::Expression* whole_net_driver(const slang::ast::ValueSymbol& sym) {
  if (const auto* init = sym.getInitializer()) {
    return init;
  }
  const auto* scope = sym.getParentScope();
  if (scope == nullptr) {
    return nullptr;
  }
  for (const auto& member : scope->members()) {
    if (member.kind != slang::ast::SymbolKind::ContinuousAssign) {
      continue;
    }
    const auto& asn = member.as<slang::ast::ContinuousAssignSymbol>().getAssignment();
    if (asn.kind != ExpressionKind::Assignment) {
      continue;
    }
    const auto& ax = asn.as<slang::ast::AssignmentExpression>();
    if (ax.left().kind != ExpressionKind::NamedValue) {
      continue;  // partial-select driver is not a whole-net constant
    }
    if (&ax.left().as<slang::ast::NamedValueExpression>().symbol != &sym) {
      continue;
    }
    return &ax.right();
  }
  return nullptr;
}

// True iff `expr` reads `target` — directly, or transitively through the whole-net
// driver of any wire it references (bounded depth). Used to decide whether a
// per-element packed-array `'{...}`/`{...}` assignment is SELF-REFERENCING: an
// element driver that reads a sibling element of the same array (the false comb
// cycle), possibly through named intermediate wires (`{_T6, ...}` where
// `_T6 = f(arr[5])`, CIRCT's shift-network shape). ONLY such an array benefits
// from the per-element leaf split; a non-self-ref per-element array (independent
// elements) keeps its flat-bus lowering (bundling it into a wire tuple whose reads
// later inline away leaves a write-only tuple the prp_writer round-trip cannot
// recompile — the DivUnit `mNeg` / DataPath `fpRfWdata` regression).
bool driver_reads_target(const slang::ast::Expression& expr, const slang::ast::ValueSymbol& target,
                         absl::flat_hash_set<const slang::ast::ValueSymbol*>& visiting, int depth) {
  if (depth > 16) {
    return false;
  }
  Named_value_collector nvc;
  expr.visit(nvc);
  for (const auto* s : nvc.syms) {
    if (s == &target) {
      return true;
    }
    if (!visiting.insert(s).second) {
      continue;
    }
    if (const auto* d = whole_net_driver(*s)) {
      if (driver_reads_target(*d, target, visiting, depth + 1)) {
        return true;
      }
    }
  }
  return false;
}

// Seed a net/var's constant value into `ctx` (as an EvalContext local) by
// chasing its whole-net driver, recursively seeding the driver's own net
// leaves first. Best-effort: returns true if `sym` got a constant value.
bool seed_const_net(const slang::ast::ValueSymbol& sym, slang::ast::EvalContext& ctx,
                    absl::flat_hash_set<const slang::ast::ValueSymbol*>& visiting, int depth) {
  if (ctx.findLocal(&sym) != nullptr) {
    return true;
  }
  if (depth > 64 || !visiting.insert(&sym).second) {
    return false;  // depth cap / cycle
  }
  bool ok = false;
  if (const auto* drv = whole_net_driver(sym)) {
    Named_value_collector col;
    drv->visit(col);
    for (const auto* dep : col.syms) {
      if (dep != &sym) {
        seed_const_net(*dep, ctx, visiting, depth + 1);  // best-effort
      }
    }
    auto cv = drv->eval(ctx);
    if (!cv.bad()) {
      ctx.createLocal(&sym, std::move(cv));
      ok = true;
    }
  }
  visiting.erase(&sym);
  return ok;
}

}  // namespace

// Fold a constant expression, chasing constant net/var drivers. firtool factors
// async-reset *values* through named constant wires (`commitStack <=
// _commitStack_WIRE;` where `_commitStack_WIRE = {e15,...,e0}` and each
// `e = '{retAddr: addr_n, ctr: 0}` and `addr_n = _GEN` and `_GEN = '{addr: 0}`):
// the RHS is a composite expression (concat / struct pattern) whose *leaves* are
// constant nets that plain expr.eval() returns `bad` for. We seed every reachable
// constant net leaf into an IsScript EvalContext (so NamedValue::eval consults
// the seeded locals) and let slang fold the whole composite with correct
// width/packing semantics. Genuinely runtime leaves stay unseeded -> eval bad ->
// nullopt -> the caller emits unsupported-async-load.
std::optional<slang::ConstantValue> Slang_context::try_eval_const_net(const slang::ast::Expression& expr, int /*depth*/) {
  if (auto cv = try_eval(expr); cv) {
    return cv;  // already fully constant
  }
  if (body_ == nullptr) {
    return std::nullopt;
  }
  // IsScript bypasses NamedValueExpression::checkConstant so seeded module-level
  // nets resolve via findLocal.
  slang::ast::EvalContext ctx(body_->asSymbol(), slang::ast::EvalFlags::IsScript);
  absl::flat_hash_set<const slang::ast::ValueSymbol*> visiting;
  Named_value_collector col;
  expr.visit(col);
  for (const auto* dep : col.syms) {
    seed_const_net(*dep, ctx, visiting, 0);
  }
  auto cv = expr.eval(ctx);
  if (cv.bad()) {
    return std::nullopt;
  }
  return cv;
}

std::string Slang_context::module_name_of(const slang::ast::InstanceSymbol& symbol) {
  const auto* body = symbol.getCanonicalBody();
  if (body == nullptr) {
    body = &symbol.body;
  }
  auto it = module_names_.find(body);
  if (it != module_names_.end()) {
    return it->second;
  }
  // Distinct parameterizations of one definition need distinct unit names;
  // the first (or only) body keeps the verilog name so --top/LEC line up.
  std::string base{symbol.getDefinition().name};
  std::string name = base;
  int         n    = 0;
  while (module_names_used_.contains(name)) {
    name = absl::StrCat(base, "_p", ++n);
  }
  module_names_used_.insert(name);
  module_names_.emplace(body, name);
  return name;
}

void Slang_context::emit_module_io(const slang::ast::InstanceSymbol& symbol, const Lnast_nid& in_tup,
                                   const Lnast_nid& out_tup) {
  // Register the ports of the CANONICAL body — the same body lower_module walks
  // (line: `body = symbol.getCanonicalBody()`). slang deduplicates structurally
  // identical instances and shares one canonical body; the port `internalSymbol`
  // the module body actually references belongs to that canonical body. When the
  // lowering of a module is triggered by a NON-canonical duplicate (common in a
  // large, multi-threaded elaboration where the first instance reached in our
  // depth-first walk is not the one slang picked as canonical), symbol.body's
  // port nets are DIFFERENT symbols than the canonical body's — same name, other
  // pointer. Registering symbol.body's nets here while the body resolves to the
  // canonical ones makes lname_of collide every port (clock/reads) into a
  // `<name>_sN` uniquified name, so tolg later rejects the dangling clock_pin
  // ("reg names clock_pin 'RW0_clk_s1' but module has no such input/wire").
  const auto* canon = symbol.getCanonicalBody();
  const auto& body  = canon != nullptr ? *canon : symbol.body;
  for (const auto& p : body.getPortList()) {
    if (p->kind == SymbolKind::Port) {
      const auto& port = p->as<slang::ast::PortSymbol>();

      if (port.direction == slang::ast::ArgumentDirection::InOut || port.direction == slang::ast::ArgumentDirection::Ref) {
        emit_unsupported(port.location, "unsupported-inout-port",
                         std::string("port '") + std::string(port.name) + "' is inout/ref, which --reader slang cannot lower");
        continue;
      }

      const auto& type   = port.getType();
      const bool  is_out = port.direction == slang::ast::ArgumentDirection::Out;

      // Unpacked-array port `T arr[N-1:0]` -> flat packed [N*elem_bits-1:0] IO
      // (Verilator/yosys flatten unpacked ports identically, so LEC lines up);
      // element access lowers to a bit-slice (see flat_port_read/write).
      bool     is_flat_array = false;
      int      io_bits       = 0;
      bool     io_signed     = false;
      Mem_info flat_mi;
      if (!type.isIntegral()) {
        const auto& ct = type.getCanonicalType();
        if (ct.kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
          const auto& arr  = ct.as<slang::ast::FixedSizeUnpackedArrayType>();
          const auto& elem = arr.elementType.getCanonicalType();
          if (elem.isIntegral() && !elem.isUnpackedArray()) {
            auto ei            = tinfo(elem);
            flat_mi.lower      = arr.range.lower();
            flat_mi.elem_bits  = ei.bits;
            flat_mi.elem_signed = ei.is_signed;
            flat_mi.size       = arr.range.width();
            io_bits            = ei.bits * static_cast<int>(flat_mi.size);
            io_signed          = false;  // flattened bus is just bits
            is_flat_array      = true;
          }
        }
        if (!is_flat_array) {
          emit_unsupported(port.location, "unsupported-port-type",
                           std::string("port '") + std::string(port.name) + "' has a non-integral type");
          continue;
        }
      } else {
        auto ti   = tinfo(type);
        io_bits   = ti.bits;
        io_signed = ti.is_signed;
      }

      // The inside-the-module symbol the body references.
      const auto* internal = port.internalSymbol;
      std::string var_name{port.name};
      {
        bool plain = !var_name.empty() && !std::isdigit(static_cast<unsigned char>(var_name.front()));
        for (const char c : var_name) {
          plain &= std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
        }
        if (!plain) {
          var_name = absl::StrCat("`", var_name, "`");  // escaped verilog identifier
        }
      }
      if (internal != nullptr) {
        sym_lname_.emplace(internal, var_name);
        used_names_.insert(var_name);
        declared_.insert(internal);  // io entries ARE the declaration
        if (is_out) {
          output_syms_.insert(internal);
          output_info_.emplace(internal, std::pair<int, bool>{io_bits, io_signed});
        } else {
          input_syms_.insert(internal);
        }
        if (is_flat_array) {
          flat_port_syms_.insert(internal);
          mem_info_.emplace(internal, flat_mi);
        }
      }

      if (!is_flat_array && type.hasFixedRange() && !type.getFixedRange().isDescending()) {
        emit_warning(slang::SourceRange(port.location, port.location), "big-endian-port", "io",
                     std::string("port '") + std::string(port.name)
                         + "' is big-endian; flipping IO (mind mix/match with other modules)");
      }

      auto& ln    = *builder_.lnast;
      auto  entry = ln.add_child(is_out ? out_tup : in_tup, Lnast_ntype::create_store());
      ln.set_pending_srcid(mint_loc(port.location));
      ln.add_child(entry, Lnast_node::create_ref(var_name));
      ln.add_child(entry, Lnast_node::create_const("nil"));  // no default value
      emit_prim_type_int(entry, io_bits, io_signed);
      if (is_out) {
        // `@[]` landing-cycle opt-out: the form foreign Verilog modules, which
        // carry no timing markings, ingest as.
        auto st = ln.add_child(entry, Lnast_ntype::create_stages());
        ln.add_child(st, Lnast_node::create_const("nil"));
        ln.add_child(st, Lnast_node::create_const("nil"));
      }
      ln.set_pending_srcid(hhds::SourceId_invalid);
    } else if (p->kind == SymbolKind::InterfacePort) {
      emit_unsupported(p->location, "unsupported-interface-port",
                       std::string("interface port '") + std::string(p->name) + "' is not supported by --reader slang",
                       "use --reader yosys-slang for interface ports");
    } else if (p->kind == SymbolKind::MultiPort) {
      emit_unsupported(p->location, "unsupported-multi-port",
                       std::string("multi-port '") + std::string(p->name) + "' is not supported by --reader slang yet");
    } else {
      emit_unsupported(p->location, "unsupported-port-kind",
                       std::string("port '") + std::string(p->name) + "' has an unsupported kind");
    }
  }
}

// Pass 1 of the module conversion: classify processes and decide which
// variables are clocked state (reg_syms_). A variable is state when an
// edge-sensitive process writes it nonblocking. Blocking-written variables in
// edge processes stay process-local temps; one written there but read
// elsewhere has flop semantics this reader does not model yet -> diagnosed.
void Slang_context::collect_state_vars(const slang::ast::InstanceBodySymbol& body) {
  for (const auto& member : body.members()) {
    if (member.kind != SymbolKind::ProceduralBlock) {
      continue;
    }
    const auto& pbs = member.as<slang::ast::ProceduralBlockSymbol>();

    bool is_edge = false;
    if (pbs.procedureKind == slang::ast::ProceduralBlockKind::Always
        || pbs.procedureKind == slang::ast::ProceduralBlockKind::AlwaysFF) {
      const auto& stmt = pbs.getBody();
      if (stmt.kind == StatementKind::Timed) {
        const auto& timing = stmt.as<slang::ast::TimedStatement>().timing;
        auto        scan   = [&](const slang::ast::TimingControl& tc) {
          if (tc.kind == slang::ast::TimingControlKind::SignalEvent) {
            auto edge = tc.as<slang::ast::SignalEventControl>().edge;
            is_edge |= edge == slang::ast::EdgeKind::PosEdge || edge == slang::ast::EdgeKind::NegEdge;
          }
        };
        if (timing.kind == slang::ast::TimingControlKind::EventList) {
          for (const auto* ev : timing.as<slang::ast::EventListControl>().events) {
            scan(*ev);
          }
        } else {
          scan(timing);
        }
      }
    }
    // Latch state: an `always_latch`, or a level-sensitive (non-edge) `always`
    // whose body uses nonblocking `<=` (the inferred-latch idiom, e.g.
    // prim_clk_gate's `always @(clk_i or ...) if (!clk_i) en_latch <= ...`).
    // Such vars become Ntype_op::Latch in tolg (din = cond?d:q, enable = cond);
    // their `<=` writes are collected below. A non-edge `always` with only
    // blocking `=` writes is plain comb (no nonblocking syms → nothing added).
    const bool is_latch_block = pbs.procedureKind == slang::ast::ProceduralBlockKind::AlwaysLatch
                                || (!is_edge && pbs.procedureKind == slang::ast::ProceduralBlockKind::Always);
    if (!is_edge && !is_latch_block) {
      continue;
    }

    Write_collector wc;
    pbs.getBody().visit(wc);
    for (const auto* sym : wc.nonblocking) {
      reg_syms_.insert(sym);
      if (is_latch_block) {
        latch_syms_.insert(sym);
      }
    }
  }
}

bool Slang_context::lower_module(const slang::ast::InstanceSymbol& symbol) {
  const auto* body = symbol.getCanonicalBody();
  if (body == nullptr) {
    body = &symbol.body;
  }

  if (auto it = lowered_.find(body); it != lowered_.end()) {
    return it->second != nullptr;  // already done (or already failed)
  }

  if (!symbol.isModule()) {
    emit_unsupported(symbol.location, "unsupported-instance-kind",
                     std::string("'") + std::string(symbol.name) + "' is not a module (interfaces/programs unsupported)");
    return false;
  }

  lowered_.emplace(body, nullptr);  // breaks recursion; reinserted at the end

  auto unit_name = module_name_of(symbol);

  // Save in-flight per-module state (a submodule definition lowers
  // recursively from its instantiation site).
  auto saved_builder       = std::move(builder_);
  auto saved_body          = body_;
  auto saved_eval          = std::move(eval_ctx_);
  auto saved_sym_lname     = std::move(sym_lname_);
  auto saved_used          = std::move(used_names_);
  auto saved_inputs        = std::move(input_syms_);
  auto saved_outputs       = std::move(output_syms_);
  auto saved_output_info   = std::move(output_info_);
  auto saved_regs          = std::move(reg_syms_);
  auto saved_wires         = std::move(wire_syms_);
  auto saved_latches       = std::move(latch_syms_);
  auto saved_mems          = std::move(mem_syms_);
  auto saved_declared      = std::move(declared_);
  auto saved_prefix        = std::move(genblk_prefix_);
  auto saved_failed        = module_failed_;
  auto saved_proc_kind     = proc_kind_;
  auto saved_style         = std::move(proc_assign_style_);
  auto saved_blocking      = std::move(proc_blocking_written_);
  auto saved_bools         = std::move(bool_values_);
  auto saved_mem_info      = std::move(mem_info_);
  auto saved_reg_declared  = std::move(reg_declared_);
  auto saved_tuple_names   = std::move(tuple_type_names_);
  auto saved_emitted_types = std::move(emitted_tuple_types_);
  auto saved_local_cnt     = local_cnt_;
  // The struct-classification collector sets are rebuilt per module below —
  // save them too, or the parent module resumes with the CHILD's sets after a
  // mid-emission recursive instance lowering, flipping is_scalar_struct_var
  // between a var's store and its later reads (Alu_3's `io_in = '{...}` stored
  // flat, but every read after the aluModule instance resolved through
  // never-written leaves -> io_out_valid stuck 0).
  auto saved_pattern_assigned = std::move(struct_pattern_assigned_);
  auto saved_field_read       = std::move(struct_field_read_);
  auto saved_deep_accessed    = std::move(struct_deep_accessed_);
  auto saved_whole_copied     = std::move(struct_whole_copied_);
  auto saved_deep_written     = std::move(struct_deep_written_);
  auto saved_array_bundle     = std::move(struct_array_bundle_);
  auto saved_packed_mem_regs  = std::move(packed_mem_regs_);

  builder_ = Lnast_builder();
  sym_lname_.clear();
  used_names_.clear();
  input_syms_.clear();
  output_syms_.clear();
  output_info_.clear();
  reg_syms_.clear();
  wire_syms_.clear();
  latch_syms_.clear();
  mem_syms_.clear();
  declared_.clear();
  genblk_prefix_.clear();
  module_failed_ = false;
  proc_kind_     = Proc_kind::none;
  proc_assign_style_.clear();
  proc_blocking_written_.clear();
  bool_values_.clear();
  mem_info_.clear();
  reg_declared_.clear();
  tuple_type_names_.clear();
  emitted_tuple_types_.clear();
  packed_mem_regs_.clear();
  local_cnt_ = 0;

  body_ = body;
  eval_ctx_.emplace(body->asSymbol(), slang::ast::EvalFlags::CacheResults);

  builder_.lnast = std::make_shared<Lnast>(unit_name);
  builder_.lnast->set_lambda_kind("mod");
  builder_.lnast->set_verilog_origin(true);  // Verilog flops are state, not pyrope feedforward stages
  auto root_nid = builder_.lnast->set_root(Lnast_ntype::create_top());
  builder_.lnast->set_srcid(root_nid, mint_loc(symbol.location));
  auto io_nid  = builder_.lnast->add_child(root_nid, Lnast_ntype::create_io());
  auto in_tup  = builder_.lnast->add_child(io_nid, Lnast_ntype::create_tuple_add());
  auto out_tup = builder_.lnast->add_child(io_nid, Lnast_ntype::create_tuple_add());
  builder_.idx_stmts = builder_.lnast->add_child(root_nid, Lnast_ntype::create_stmts());

  emit_module_io(symbol, in_tup, out_tup);
  collect_state_vars(*body);
  struct_pattern_assigned_.clear();
  collect_struct_pattern_assigns(*body);
  struct_field_read_.clear();
  {
    Member_read_collector mrc;
    mrc.out = &struct_field_read_;
    body->visit(mrc);
  }
  struct_deep_accessed_.clear();
  {
    Deep_struct_access_collector dac;
    dac.out = &struct_deep_accessed_;
    body->visit(dac);
  }
  struct_whole_copied_.clear();
  {
    Struct_whole_copy_collector swc;
    swc.out = &struct_whole_copied_;
    body->visit(swc);
  }
  struct_deep_written_.clear();
  {
    Deep_struct_write_collector dwc;
    dwc.out = &struct_deep_written_;
    body->visit(dwc);
  }
  struct_array_bundle_.clear();
  {
    absl::flat_hash_map<const slang::ast::ValueSymbol*, Array_bundle_collector::Info> info;
    Array_bundle_collector                                                            abc;
    abc.info = &info;
    body->visit(abc);
    for (const auto& [sym, in] : info) {
      // Exactly one whole per-element driver, read ONLY by constant single-element
      // select, and no element/partial writes: a candidate for the per-element leaf
      // split. A dynamic-index or range read keeps it flat (no per-element leaf to
      // route such an access to).
      if (!(in.whole_drivers == 1 && in.per_elem_driver && in.elem_read && !in.elem_written
            && !in.nonconst_access)) {
        continue;
      }
      // Only SELF-REFERENCING arrays (an element driver reads a sibling element,
      // directly or transitively) actually need the split — that is the false comb
      // cycle. A non-self-ref per-element array (independent elements) must keep its
      // flat bus: bundling it into a wire tuple whose reads later inline away leaves
      // a write-only tuple the prp_writer round-trip cannot recompile.
      const auto* drv = whole_net_driver(*sym);
      if (drv == nullptr) {
        continue;
      }
      absl::flat_hash_set<const slang::ast::ValueSymbol*> visiting;
      if (driver_reads_target(*drv, *sym, visiting, 0)) {
        struct_array_bundle_.insert(sym);
      }
    }
  }
  // Harvest `initial begin mem[k]=v; end` power-on contents before the declares
  // emit (declare_unpacked folds them into the reg array's initializer).
  for (const auto& member : body->members()) {
    if (member.kind == slang::ast::SymbolKind::ProceduralBlock
        && member.as<slang::ast::ProceduralBlockSymbol>().procedureKind == slang::ast::ProceduralBlockKind::Initial) {
      collect_mem_inits(member.as<slang::ast::ProceduralBlockSymbol>().getBody());
    }
  }
  // Classify each unpacked array as constant- or runtime-indexed (a non-const
  // selector anywhere makes it runtime-indexed) BEFORE any declare runs:
  // declare_unpacked (reg hoisting below, and the comb loop) consults
  // runtime_indexed_arrays_ to choose flatten (const-indexed -> packed bus,
  // matching yosys-slang) vs memory (runtime-indexed). A comb plain-vector
  // array that is never runtime-indexed is safe to flatten; a runtime-indexed
  // one stays a memory.
  {
    Array_index_collector aic;
    body->visit(aic);
    auto is_runtime_sel = [&](const slang::ast::Expression* sel) {
      if (try_eval_int(*sel)) {
        return false;  // folds to a constant (genvar/param/const index)
      }
      // Not directly foldable: still constant after unroll if it references only
      // loop-induction vars / params / genvars. A runtime signal makes it dynamic.
      Static_selector_scan ss;
      ss.loop_vars = &aic.loop_vars;
      sel->visit(ss);
      return !ss.all_static;
    };
    for (const auto& [sym, sel] : aic.selects) {
      if (is_runtime_sel(sel)) {
        runtime_indexed_arrays_.insert(sym);
      }
    }
    // A PACKED 2-D reg `[N][W]` (W>1) that is RUNTIME-indexed somewhere is a
    // register file: memory-ize it (one __memory node) so it LECs against an
    // equivalent Pyrope memory, instead of flattening to one wide N*W-bit flop.
    // A const-indexed-only packed 2-D reg keeps the flat-bus path (yosys-slang
    // compatibility); any runtime select anywhere marks the whole array.
    for (const auto& [sym, sel] : aic.packed_selects) {
      int64_t n = 0, lo = 0;
      int     w = 0;
      bool    sg = false;
      if (reg_syms_.contains(sym) && is_packed_2d_array(sym->getType(), n, w, sg, lo) && is_runtime_sel(sel)) {
        packed_mem_regs_.insert(sym);
      }
    }
  }
  // Hoist every state reg's declare to module start: drivers emit in
  // dataflow order, so a comb reader sorted before the owning edge process
  // must already see the declare (reg q-reads are order-free only once
  // declared). Output regs declare here too - the q pin IS the output.
  // reg_syms_ is a pointer-keyed flat_hash_set whose iteration order abseil
  // perturbs by the table's heap address (so it varies run-to-run under ASLR).
  // Emitting the declares — and the order-sensitive lname_of `_sN` name
  // uniquing — in that order makes the IR (and occasionally the generated
  // signal names) nondeterministic. Emit in a stable source-location order.
  {
    std::vector<const slang::ast::Symbol*> regs(reg_syms_.begin(), reg_syms_.end());
    std::sort(regs.begin(), regs.end(), [](const slang::ast::Symbol* a, const slang::ast::Symbol* b) {
      if (a->location != b->location) {
        return a->location < b->location;
      }
      return a->name < b->name;
    });
    for (const auto* sym : regs) {
      declare_reg(sym->as<slang::ast::ValueSymbol>());
    }
  }

  // Pre-declare COMBINATIONAL flattenable arrays as flat packed buses
  // (declare_unpacked's flatten branch): they must be declared before any
  // element access lowers, so resolve_packed_lvalue and the read path see them
  // as flat (bit-slice) symbols rather than memories. Flatten an array of a
  // packed AGGREGATE (struct/union — its `.field` writes need composition) or a
  // plain-vector array that is never runtime-indexed (its bit-slice writes need
  // composition too, and constant offsets make flattening exact).
  for (const auto& member : body->members()) {
    if (member.kind != slang::ast::SymbolKind::Variable) {
      continue;
    }
    const auto& vsym = member.as<slang::ast::VariableSymbol>();
    if (reg_syms_.contains(&vsym) || declared_.contains(&vsym)) {
      continue;
    }
    const auto& ct = vsym.getType().getCanonicalType();
    if (ct.kind != slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
      continue;
    }
    const auto& elem = ct.as<slang::ast::FixedSizeUnpackedArrayType>().elementType.getCanonicalType();
    if (!elem.isIntegral()) {
      continue;
    }
    const bool aggregate    = elem.isStruct() || elem.isPackedUnion();
    const bool const_indexed = !runtime_indexed_arrays_.contains(&vsym);
    if (aggregate || const_indexed) {
      declare_value_symbol(vsym, /*force_reg=*/false);
    }
  }

  // X-default poison-init for every COMBINATIONAL (non-reg) output: emit
  // `out = 0sb?` / `0ub?…?` at body top, BEFORE lower_members drives anything.
  // A legal Verilog output the body never drives defaults to X — this makes that
  // explicit (Pyrope would otherwise reject an undriven output). A real driver
  // supersedes it (the coalescer DSE-drops it, or a conditional drive folds it to
  // the mux ELSE arm = correct X-when-not-selected). This is what lets tolg drop
  // its is_verilog_origin undriven-output leniency and keep the hard error for a
  // genuinely-undriven PYROPE output. Output regs already have a q-pin driver, so
  // they are excluded (a poison-init would double-drive). Deterministic order.
  {
    std::vector<const slang::ast::Symbol*> couts;
    for (const auto* sym : output_syms_) {
      if (reg_syms_.contains(sym) || !output_info_.contains(sym) || !sym_lname_.contains(sym)) {
        continue;
      }
      // Skip an ESCAPED (`\`-quoted, i.e. dotted) output name — that is a
      // struct/tuple LEAF (`p.q`), driven through a shadow-mut that already
      // X-fills its undriven leaves. A whole-output poison here fights that
      // machinery and re-emits as a malformed nested-backtick name. Only plain
      // SCALAR outputs (which the body drives — or not — as a whole) get poison.
      const auto& ln = sym_lname_.at(sym);
      if (!ln.empty() && ln.front() == '`') {
        continue;
      }
      couts.push_back(sym);
    }
    std::sort(couts.begin(), couts.end(), [](const slang::ast::Symbol* a, const slang::ast::Symbol* b) {
      if (a->location != b->location) {
        return a->location < b->location;
      }
      return a->name < b->name;
    });
    for (const auto* sym : couts) {
      const auto [bits, is_signed] = output_info_.at(sym);
      set_pending_loc(sym->location);
      if (is_signed) {
        builder_.create_assign_stmts(sym_lname_.at(sym), "0sb?");
      } else {
        std::string qmarks(static_cast<size_t>(bits > 0 ? bits : 1), '?');
        builder_.create_assign_stmts(sym_lname_.at(sym), absl::StrCat("0ub", qmarks));
      }
      clear_pending_loc();
    }
  }

  lower_members(*body);

  bool ok = !module_failed_;
  if (ok) {
    lowered_[body] = builder_.lnast;
    ordered_lnasts_.push_back(builder_.lnast);
  }

  // restore the enclosing module's state
  builder_               = std::move(saved_builder);
  body_                  = saved_body;
  eval_ctx_              = std::move(saved_eval);
  sym_lname_             = std::move(saved_sym_lname);
  used_names_            = std::move(saved_used);
  input_syms_            = std::move(saved_inputs);
  output_syms_           = std::move(saved_outputs);
  output_info_           = std::move(saved_output_info);
  reg_syms_              = std::move(saved_regs);
  wire_syms_            = std::move(saved_wires);
  latch_syms_           = std::move(saved_latches);
  mem_syms_             = std::move(saved_mems);
  declared_              = std::move(saved_declared);
  genblk_prefix_         = std::move(saved_prefix);
  module_failed_         = saved_failed;
  proc_kind_             = saved_proc_kind;
  proc_assign_style_     = std::move(saved_style);
  proc_blocking_written_ = std::move(saved_blocking);
  bool_values_           = std::move(saved_bools);
  mem_info_              = std::move(saved_mem_info);
  reg_declared_          = std::move(saved_reg_declared);
  tuple_type_names_      = std::move(saved_tuple_names);
  emitted_tuple_types_   = std::move(saved_emitted_types);
  local_cnt_             = saved_local_cnt;
  struct_pattern_assigned_ = std::move(saved_pattern_assigned);
  struct_field_read_       = std::move(saved_field_read);
  struct_deep_accessed_    = std::move(saved_deep_accessed);
  struct_whole_copied_     = std::move(saved_whole_copied);
  struct_deep_written_     = std::move(saved_deep_written);
  struct_array_bundle_     = std::move(saved_array_bundle);
  packed_mem_regs_         = std::move(saved_packed_mem_regs);

  return ok;
}

// comp_type_array declare for an unpacked array (a verilog memory). The
// fwd=0 attr rides ahead of the declare: verilog nonblocking memory writes
// never forward to same-cycle reads (Pyrope reg arrays default to
// program-order forwarding).
// Walk an `initial` block body, recording constant `mem[const] = const` element
// writes (the standard memory power-on idiom) into mem_init_vals_. Only simple
// element-select-of-named-value assignments with foldable index/value are
// captured; anything else is silently skipped (the block stays "ignored").
void Slang_context::collect_mem_inits(const slang::ast::Statement& stmt) {
  using slang::ast::ExpressionKind;
  using slang::ast::StatementKind;
  switch (stmt.kind) {
    case StatementKind::List:
      for (const auto* s : stmt.as<slang::ast::StatementList>().list) {
        collect_mem_inits(*s);
      }
      return;
    case StatementKind::Block: collect_mem_inits(stmt.as<slang::ast::BlockStatement>().body); return;
    case StatementKind::ExpressionStatement: {
      const auto& e = stmt.as<slang::ast::ExpressionStatement>().expr;
      if (e.kind != ExpressionKind::Assignment) {
        return;
      }
      const auto& as  = e.as<slang::ast::AssignmentExpression>();
      const auto& lhs = as.left();
      if (lhs.kind != ExpressionKind::ElementSelect) {
        return;
      }
      const auto& es = lhs.as<slang::ast::ElementSelectExpression>();
      if (es.value().kind != ExpressionKind::NamedValue) {
        return;
      }
      const auto* sym = &es.value().as<slang::ast::NamedValueExpression>().symbol;
      auto        idx = try_eval_int(es.selector());
      auto        val = try_eval_int(as.right());
      if (idx && val && *idx >= 0) {
        mem_init_vals_[sym][*idx] = *val;
      }
      return;
    }
    default: return;
  }
}

bool Slang_context::declare_unpacked(const slang::ast::ValueSymbol& sym, bool is_reg) {
  const auto& ct = sym.getType().getCanonicalType();
  if (ct.kind != slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
    emit_unsupported(sym.location, "unsupported-array-kind",
                     std::string("array '") + std::string(sym.name) + "' is not a fixed-size unpacked array");
    return false;
  }
  const auto& arr  = ct.as<slang::ast::FixedSizeUnpackedArrayType>();
  const auto& elem = arr.elementType.getCanonicalType();
  if (!elem.isIntegral() || elem.isUnpackedArray()) {
    emit_unsupported(sym.location, "unsupported-mem-element",
                     std::string("memory '") + std::string(sym.name)
                         + "' has a non-integral or multi-dimensional element type");
    return false;
  }
  auto ei = tinfo(elem);

  // FLATTEN a COMBINATIONAL array of a packed AGGREGATE (struct/union/packed
  // array) to a single packed bus, matching yosys-slang (which flattens small
  // arrays to flops+mux). Element access then becomes a bit-slice, so a nested
  // `arr[const].field = v` composes via set_mask on ONE variable — a
  // memory-element read-modify-write would instead clobber sibling fields (the
  // read does not forward the prior same-process write). Restricted to non-reg
  // (comb) arrays with an aggregate element, so the clocked-memory path (FIFOs,
  // register files) and plain-scalar comb arrays are untouched.
  // Flatten a comb array to a packed bus when element accesses compose better
  // as bit-slices than as memory ports: a packed STRUCT/UNION element (its
  // `.field` sub-writes must compose) OR a plain-vector array that is never
  // runtime-indexed (its `[slice]` sub-writes compose via set_mask, and constant
  // element offsets make the flattening exact). A runtime-indexed plain-vector
  // array stays a memory (dynamic-shift flattening mismatches).
  const bool    elem_struct  = elem.isStruct() || elem.isPackedUnion();
  const bool    const_indexed = !runtime_indexed_arrays_.contains(&sym);
  const int64_t flat_bits    = static_cast<int64_t>(ei.bits) * arr.range.width();
  const bool    has_init     = [&] {
    auto it = mem_init_vals_.find(&sym);
    return it != mem_init_vals_.end() && !it->second.empty();
  }();
  // FLATTEN to a single packed bus when the array is CONST-indexed (reg OR comb)
  // — yosys-slang packs a const-indexed array into one wide word (SIZE=1), so a
  // multi-entry memory would not LEC against it — or a comb aggregate array
  // (its `.field` sub-writes compose via set_mask). Element/field/bit access
  // then becomes a bit-slice on ONE variable (set_mask, the flat-port path). A
  // RUNTIME-indexed array stays a memory (the FIFO/regfile/store path). A reg
  // array with power-on contents keeps the memory path (the init becomes the
  // mem declare initializer; flattening it would need a concatenated reset).
  if ((const_indexed || (!is_reg && elem_struct)) && !(is_reg && has_init) && flat_bits > 0 && flat_bits <= 65536) {
    Mem_info fmi;
    fmi.lower       = arr.range.lower();
    fmi.elem_bits   = ei.bits;
    fmi.elem_signed = ei.is_signed;
    fmi.size        = arr.range.width();
    mem_info_.emplace(&sym, fmi);
    mem_syms_.insert(&sym);
    flat_port_syms_.insert(&sym);  // reuse the flat bit-slice get/set machinery
    auto name = lname_of(sym);
    set_pending_loc(sym.location);
    if (is_reg) {
      // A packed REG bus: unwritten bits hold (reg), set_mask writes compose in
      // program order (later overrides win) — matching yosys-slang's flops+mux.
      // No reset (the soomrv const-indexed reg banks reset explicitly in-body).
      builder_.create_declare_stmts(name, "reg", mask_text(static_cast<int>(flat_bits)), "0", "nil");
    } else {
      builder_.create_declare_stmts(name, "mut", mask_text(static_cast<int>(flat_bits)), "0");
    }
    clear_pending_loc();
    return true;
  }

  // STRUCT-element memory -> a TUPLE-typed memory `reg mem:[N]T`. The reader
  // emits a `type T=(...)` region + a `comp_type_array(ref T,[N])` declare;
  // upass.detuple splits it into per-field scalar memories `mem.field:[N]w`.
  // Every element access (field, and decomposed whole-element) lowers to
  // field-level tuple ops, so detuple only ever sees field-level ops.
  if (elem.isStruct()) {
    const auto& st = elem.as<slang::ast::PackedStructType>();
    Mem_info    mi;
    mi.lower       = arr.range.lower();
    mi.elem_bits   = ei.bits;
    mi.elem_signed = false;
    mi.size        = arr.range.width();
    mi.is_tuple    = true;
    mi.type_name   = tuple_type_name(elem);
    for (const auto& f : st.membersOfType<slang::ast::FieldSymbol>()) {
      auto fi = tinfo(f.getType());
      mi.fields.push_back({std::string(f.name), static_cast<int64_t>(f.bitOffset), fi.bits, fi.is_signed});
    }
    mem_info_.emplace(&sym, mi);
    mem_syms_.insert(&sym);

    const auto& rec  = mem_info_.at(&sym);
    auto        name = lname_of(sym);
    auto&       ln   = *builder_.lnast;
    set_pending_loc(sym.location);
    emit_tuple_typedef(rec);
    // Verilog nonblocking memory reads see old contents (fwd=0). detuple does
    // not split attr_set, so emit the attr on each post-split `mem.field` name
    // directly (it lands before the per-field declares detuple synthesizes).
    if (is_reg) {
      for (const auto& f : rec.fields) {
        auto aidx = builder_.add_child(Lnast_ntype::create_attr_set());
        ln.add_child(aidx, Lnast_node::create_ref(absl::StrCat(name, ".", f.name)));
        ln.add_child(aidx, Lnast_node::create_const("fwd"));
        ln.add_child(aidx, Lnast_node::create_const("0"));
      }
    }
    auto didx = builder_.add_child(Lnast_ntype::create_declare());
    ln.add_child(didx, Lnast_node::create_ref(name));
    auto tidx = ln.add_child(didx, Lnast_ntype::create_comp_type_array());
    ln.add_child(tidx, Lnast_node::create_ref(rec.type_name));
    ln.add_child(tidx, Lnast_node::create_const(absl::StrCat("[", rec.size, "]")));
    ln.add_child(didx, Lnast_node::create_const(is_reg ? "reg" : "mut"));
    ln.add_child(didx, Lnast_node::create_const("nil"));
    clear_pending_loc();
    return true;
  }

  Mem_info mi;
  mi.lower       = arr.range.lower();
  mi.elem_bits   = ei.bits;
  mi.elem_signed = ei.is_signed;
  mi.size        = arr.range.width();
  mem_info_.emplace(&sym, mi);
  mem_syms_.insert(&sym);

  auto  name = lname_of(sym);
  auto& ln   = *builder_.lnast;
  set_pending_loc(sym.location);
  if (is_reg) {
    auto aidx = builder_.add_child(Lnast_ntype::create_attr_set());
    ln.add_child(aidx, Lnast_node::create_ref(name));
    ln.add_child(aidx, Lnast_node::create_const("fwd"));
    ln.add_child(aidx, Lnast_node::create_const("0"));
  }
  auto didx = builder_.add_child(Lnast_ntype::create_declare());
  ln.add_child(didx, Lnast_node::create_ref(name));
  auto tidx = ln.add_child(didx, Lnast_ntype::create_comp_type_array());
  emit_prim_type_int(tidx, ei.bits, ei.is_signed);
  ln.add_child(tidx, Lnast_node::create_const(absl::StrCat("[", mi.size, "]")));
  ln.add_child(didx, Lnast_node::create_const(is_reg ? "reg" : "mut"));
  // Power-on contents from an `initial` block: a uniform fill becomes a scalar
  // broadcast (`= 3`); a per-entry fill a tuple literal (`= (1,2,3,4)`), index
  // order with un-written entries defaulting to 0.  Absent → `nil` (reg only).
  if (auto iit = mem_init_vals_.find(&sym); iit != mem_init_vals_.end() && !iit->second.empty()) {
    const auto& vals    = iit->second;
    const bool  uniform = std::all_of(vals.begin(), vals.end(),
                                      [&](const auto& kv) { return kv.second == vals.begin()->second; });
    if (uniform && static_cast<int64_t>(vals.size()) == mi.size) {
      ln.add_child(didx, Lnast_node::create_const(absl::StrCat(vals.begin()->second)));
    } else {
      auto vidx = ln.add_child(didx, Lnast_ntype::create_tuple_add());
      for (int64_t k = 0; k < mi.size; ++k) {
        auto vit = vals.find(k);
        ln.add_child(vidx, Lnast_node::create_const(absl::StrCat(vit != vals.end() ? vit->second : int64_t{0})));
      }
    }
  } else if (is_reg) {
    ln.add_child(didx, Lnast_node::create_const("nil"));  // no power-on contents
  }
  clear_pending_loc();
  return true;
}

// Stable per-module name for a struct element type (keyed by the canonical type
// pointer so two memories of the same struct reuse one typedef). Uses the SV
// type name when present; synthesizes a unique one for anonymous structs.
std::string Slang_context::tuple_type_name(const slang::ast::Type& elem) {
  auto [it, ins] = tuple_type_names_.try_emplace(&elem, std::string{});
  if (ins) {
    std::string n{elem.name};
    if (n.empty()) {
      // Synthesized name for an anonymous struct element. Must NOT start with
      // `__` (reserved for compiler temps; upass.bundle asserts on it) and must
      // not collide with a user identifier — capitalized `Styp_` is type-like.
      n = absl::StrCat("Styp_", tuple_type_names_.size());
    }
    it->second = std::move(n);
  }
  return it->second;
}

// Emit a `type T=(...)` region (once per type per module) in the no-default form
// upass.detuple's resolve_one_type recognizes:
//   declare(ref T, prim_type_none, const 'type')
//   type_spec(ref field, prim_type_int(max,min))            // one per field
//   tuple_add(ref Ttemp, ref field0, ref field1, …)         // field order
//   store(ref T, ref Ttemp)                                 // region terminator
void Slang_context::emit_tuple_typedef(const Mem_info& mi) {
  if (!mi.is_tuple || mi.type_name.empty() || mi.fields.empty()) {
    return;
  }
  if (!emitted_tuple_types_.insert(mi.type_name).second) {
    return;  // already emitted in this module
  }
  auto& ln = *builder_.lnast;

  auto d = builder_.add_child(Lnast_ntype::create_declare());
  ln.add_child(d, Lnast_node::create_ref(mi.type_name));
  ln.add_child(d, Lnast_ntype::create_prim_type_none());
  ln.add_child(d, Lnast_node::create_const("type"));

  for (const auto& f : mi.fields) {
    auto ts = builder_.add_child(Lnast_ntype::create_type_spec());
    ln.add_child(ts, Lnast_node::create_ref(f.name));
    emit_prim_type_int(ts, f.bits, f.is_signed);
  }

  auto ttemp = builder_.create_lnast_tmp();
  auto ta    = builder_.add_child(Lnast_ntype::create_tuple_add());
  ln.add_child(ta, Lnast_node::create_ref(ttemp));
  for (const auto& f : mi.fields) {
    ln.add_child(ta, Lnast_node::create_ref(f.name));
  }

  auto st = builder_.add_child(Lnast_ntype::create_store());
  ln.add_child(st, Lnast_node::create_ref(mi.type_name));
  ln.add_child(st, Lnast_node::create_ref(ttemp));
}

// A packed 2-D array `reg [N-1:0][W-1:0]` (W>1): canonical type is a
// PackedArrayType whose ELEMENT canonical type is itself an integral vector of
// width > 1. A 1-D packed vector `reg [W-1:0]` has a single-bit element (an
// element-select is a bit-select) — excluded. Reports N/W/sign/lower on hit.
bool Slang_context::is_packed_2d_array(const slang::ast::Type& type, int64_t& size, int& elem_bits, bool& elem_signed,
                                       int64_t& lower) {
  const auto& ct = type.getCanonicalType();
  if (!ct.isPackedArray() || ct.kind != slang::ast::SymbolKind::PackedArrayType) {
    return false;
  }
  const auto& pa   = ct.as<slang::ast::PackedArrayType>();
  const auto& elem = pa.elementType.getCanonicalType();
  // The element must be a >1-bit integral value: a packed vector/array/struct.
  // (A struct element falls through too — but the regfile case is a plain
  // vector; struct-element packed arrays are rare and still bit-slice fine.)
  if (!elem.isIntegral() || elem.getBitWidth() <= 1) {
    return false;
  }
  size        = pa.range.width();
  elem_bits   = static_cast<int>(elem.getBitWidth());
  elem_signed = elem.isSigned();
  lower       = pa.range.lower();
  return true;
}

// State regs declare once at module start, output regs included (ports sit
// in declared_ from the io emission, hence the dedicated reg_declared_ set).
void Slang_context::declare_reg(const slang::ast::ValueSymbol& sym) {
  if (reg_declared_.contains(&sym)) {
    return;
  }
  reg_declared_.insert(&sym);
  declared_.insert(&sym);

  const auto& type = sym.getType();
  if (type.getCanonicalType().isUnpackedArray()) {
    declare_unpacked(sym, /*is_reg=*/true);
    return;
  }
  // A runtime-indexed PACKED 2-D reg `[N-1:0][W-1:0]` (W>1) is a register file:
  // declare it as a scalar-element MEMORY `reg name:[N]uW = nil` (mirrors the
  // non-flatten scalar branch of declare_unpacked) so element reads/writes route
  // through the __memory path and LEC against an equivalent Pyrope memory. A
  // plain flat flop (the fall-through below) cannot align with a memory node.
  if (packed_mem_regs_.contains(&sym)) {
    int64_t n = 0, lo = 0;
    int     w = 0;
    bool    sg = false;
    if (is_packed_2d_array(type, n, w, sg, lo)) {
      Mem_info mi;
      mi.lower       = lo;
      mi.elem_bits   = w;
      mi.elem_signed = sg;
      mi.size        = n;
      mi.is_tuple    = false;
      mem_info_.emplace(&sym, mi);
      mem_syms_.insert(&sym);  // NOT flat_port_syms_: this routes via store/tuple_get

      auto  name = lname_of(sym);
      auto& ln   = *builder_.lnast;
      set_pending_loc(sym.location);
      // Verilog nonblocking memory reads see old (committed) contents — fwd=0.
      {
        auto aidx = builder_.add_child(Lnast_ntype::create_attr_set());
        ln.add_child(aidx, Lnast_node::create_ref(name));
        ln.add_child(aidx, Lnast_node::create_const("fwd"));
        ln.add_child(aidx, Lnast_node::create_const("0"));
      }
      auto didx = builder_.add_child(Lnast_ntype::create_declare());
      ln.add_child(didx, Lnast_node::create_ref(name));
      auto tidx = ln.add_child(didx, Lnast_ntype::create_comp_type_array());
      emit_prim_type_int(tidx, w, sg);
      ln.add_child(tidx, Lnast_node::create_const(absl::StrCat("[", n, "]")));
      ln.add_child(didx, Lnast_node::create_const("reg"));
      // Power-on contents from an `initial` block (same shape as declare_unpacked):
      // uniform fill -> scalar broadcast, per-entry fill -> tuple literal, else nil.
      if (auto iit = mem_init_vals_.find(&sym); iit != mem_init_vals_.end() && !iit->second.empty()) {
        const auto& vals    = iit->second;
        const bool  uniform = std::all_of(vals.begin(), vals.end(),
                                          [&](const auto& kv) { return kv.second == vals.begin()->second; });
        if (uniform && static_cast<int64_t>(vals.size()) == mi.size) {
          ln.add_child(didx, Lnast_node::create_const(absl::StrCat(vals.begin()->second)));
        } else {
          auto vidx = ln.add_child(didx, Lnast_ntype::create_tuple_add());
          for (int64_t k = 0; k < mi.size; ++k) {
            auto vit = vals.find(k);
            ln.add_child(vidx, Lnast_node::create_const(absl::StrCat(vit != vals.end() ? vit->second : int64_t{0})));
          }
        }
      } else {
        ln.add_child(didx, Lnast_node::create_const("nil"));  // no power-on contents
      }
      clear_pending_loc();
      return;
    }
  }
  if (!type.isIntegral()) {
    emit_unsupported(sym.location, "unsupported-var-type",
                     std::string("variable '") + std::string(sym.name) + "' has a non-integral type");
    return;
  }

  auto ti   = tinfo(type);
  auto name = lname_of(sym);
  // A level-sensitive latch var lowers to Ntype_op::Latch (mode "latch"); it has
  // no clock/reset — its enable (transparency condition) and din are wired by
  // tolg's finalize_regs from the body's if-merge.
  const char* mode = latch_syms_.contains(&sym) ? "latch" : "reg";
  set_pending_loc(sym.location);
  builder_.create_declare_stmts(name,
                                mode,
                                int_max_str(ti.bits, ti.is_signed),
                                int_min_str(ti.bits, ti.is_signed),
                                "nil");  // no reset by default; async patterns override via attrs
  clear_pending_loc();
}

void Slang_context::declare_value_symbol(const slang::ast::ValueSymbol& sym, bool force_reg) {
  if (force_reg || reg_syms_.contains(&sym)) {
    declare_reg(sym);
    return;
  }
  if (declared_.contains(&sym)) {
    return;
  }
  declared_.insert(&sym);

  const auto& type = sym.getType();
  if (type.getCanonicalType().isUnpackedArray()) {
    declare_unpacked(sym, /*is_reg=*/false);
    return;
  }
  // A scalar packed-struct variable lowers to per-field leaf nets (bundle),
  // never a flat packed bus — so each field is an independent LGraph net.
  if (is_scalar_struct_var(sym)) {
    declare_struct_leaves(sym);
    return;
  }
  // A self-referencing packed-array local lowers to per-element leaf nets (the
  // array analogue of the struct bundle) so a sibling read `vec[0]` binds to its
  // own net, not the stale whole-`vec` bus (Type C false comb cycle).
  if (is_packed_array_bundle_var(sym)) {
    declare_array_leaves(sym);
    return;
  }
  if (!type.isIntegral()) {
    emit_unsupported(sym.location, "unsupported-var-type",
                     std::string("variable '") + std::string(sym.name) + "' has a non-integral type");
    return;
  }

  auto ti   = tinfo(type);
  auto name = lname_of(sym);

  // 2c-wire — a net in a combinational dependency cycle is a `wire`: its single
  // continuous driver is the net's value, and reads of it are position-
  // independent (a read before the driver binds to the resolved net). No poison
  // init (that would be a competing driver) — the driver supplies the value.
  if (wire_syms_.contains(&sym)) {
    set_pending_loc(sym.location);
    // Declare the cyclic net at its real width/sign (a Verilog net has a known
    // type). An untyped wire gets provisional width 1, restamped from its driver
    // only at finalize_wires() — AFTER any consumer is built, so a FORWARD read
    // (the whole reason this net is a `wire`) sizes against width 1. A Hotmux/Mux
    // merging that forward-ref arm then truncates to the narrow placeholder (a
    // 64-bit forwarded value collapsed to 2 bits), miscompiling the cone. Stamping
    // the declared width up front lets every forward consumer size correctly.
    builder_.create_declare_stmts(name, "wire", int_max_str(ti.bits, ti.is_signed), int_min_str(ti.bits, ti.is_signed));
    clear_pending_loc();
    return;
  }

  set_pending_loc(sym.location);
  {
    // No declared range on locals: the importer masks every op result to its
    // verilog width already, and a range would conflict with the x poison
    // init (an all-? literal reads as -1 to the bitwidth range check).
    builder_.create_declare_stmts(name, "mut", "", "");
    // poison init: a read of never-assigned bits is x, not silent 0
    if (ti.is_signed) {
      builder_.create_assign_stmts(name, "0sb?");
    } else {
      std::string qmarks(static_cast<size_t>(ti.bits), '?');
      builder_.create_assign_stmts(name, absl::StrCat("0ub", qmarks));
    }
  }
  clear_pending_loc();
}

bool Slang_context::is_packed_array_bundle_var(const slang::ast::ValueSymbol& sym) const {
  // Only a per-element-driven packed-array LOCAL (see Array_bundle_collector). Ports,
  // clocked regs, and memory-ized arrays keep their existing lowering.
  if (!struct_array_bundle_.contains(&sym)) {
    return false;
  }
  if (input_syms_.contains(&sym) || output_syms_.contains(&sym) || reg_syms_.contains(&sym)
      || mem_info_.contains(&sym)) {
    return false;
  }
  // The per-element leaf is `<base>.e<idx>`; a base name that needs backtick
  // escaping (a `$`/`:`/`\`-laden yosys-netlist name, e.g. `\0$memwr$...`) cannot
  // form a valid dotted leaf ref, so keep such an array a flat bus.
  for (char c : sym.name) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
      return false;
    }
  }
  const auto& ct = sym.getType().getCanonicalType();
  return ct.isPackedArray() && ct.isIntegral();
}

// Declare the per-ELEMENT leaf nets of a self-referencing packed array. Reuses the
// Struct_info machinery (assign_struct_whole / read_struct_whole / find_struct_field)
// with each element modeled as a field named `e<idx>` at bit offset idx*elem_bits.
// Fields are ordered HIGHEST-index-first so field[i] lines up with `'{...}` element
// [i] (a SystemVerilog array assignment pattern lists the first-declared/high index
// first, exactly like the struct field order the pattern path already assumes).
void Slang_context::declare_array_leaves(const slang::ast::ValueSymbol& sym) {
  const auto& ct        = sym.getType().getCanonicalType();
  const auto* elem_ty   = ct.getArrayElementType();
  auto        elem_ti   = tinfo(*elem_ty);
  int         elem_bits = elem_ti.bits;
  int         count     = elem_bits > 0 ? static_cast<int>(ct.getBitWidth()) / elem_bits : 0;
  Struct_info si;
  // WIRE leaves (position-independent reads): the `'{...}` pattern is emitted in
  // field order (highest index first), so an element that reads a LOWER-index
  // sibling (`vec.e1 = vec.e0 ^ a_1`) is emitted BEFORE that sibling's driver
  // (`vec.e0 = a_0`). A mut leaf would read the sibling's poison init; a wire
  // leaf binds the read to the driver regardless of textual order. The
  // element-level dependency is acyclic (that is the whole point of splitting),
  // so no real combinational cycle results.
  si.is_wire  = true;
  si.is_tuple = false;  // per-element flat leaves (never a real LNAST tuple)
  auto base   = lname_of(sym);
  set_pending_loc(sym.location);
  for (int idx = count - 1; idx >= 0; --idx) {
    si.fields.push_back({absl::StrCat("e", idx), static_cast<int64_t>(idx) * elem_bits, elem_bits, elem_ti.is_signed});
    auto leaf = absl::StrCat(base, ".e", idx);
    if (si.is_wire) {
      builder_.create_declare_stmts(leaf, "wire", int_max_str(elem_bits, elem_ti.is_signed),
                                    int_min_str(elem_bits, elem_ti.is_signed));
    } else {
      builder_.create_declare_stmts(leaf, "mut", "", "");
      if (elem_ti.is_signed) {
        builder_.create_assign_stmts(leaf, "0sb?");
      } else {
        std::string qmarks(static_cast<size_t>(elem_bits), '?');
        builder_.create_assign_stmts(leaf, absl::StrCat("0ub", qmarks));
      }
    }
  }
  clear_pending_loc();
  struct_var_info_.emplace(&sym, std::move(si));
}

bool Slang_context::struct_is_all_scalar(const slang::ast::ValueSymbol& sym) const {
  const auto& ct = sym.getType().getCanonicalType();
  if (!ct.isStruct()) {
    return false;
  }
  for (const auto& f : ct.as<slang::ast::PackedStructType>().membersOfType<slang::ast::FieldSymbol>()) {
    if (f.getType().getCanonicalType().isStruct()) {
      return false;  // nested struct field — flat-leaf whole-read does not reassemble cleanly
    }
  }
  return true;
}

bool Slang_context::whole_copied_selfref_pattern(const slang::ast::ValueSymbol& sym) const {
  if (auto it = selfref_pattern_cache_.find(&sym); it != selfref_pattern_cache_.end()) {
    return it->second;
  }
  bool ok = false;
  if (const auto* drv = whole_net_driver(sym)) {
    const auto* r = drv;
    while (r->kind == slang::ast::ExpressionKind::Conversion) {
      r = &r->as<slang::ast::ConversionExpression>().operand();
    }
    size_t n_elems = 0;
    switch (r->kind) {
      case slang::ast::ExpressionKind::SimpleAssignmentPattern:
        n_elems = r->as<slang::ast::SimpleAssignmentPatternExpression>().elements().size();
        break;
      case slang::ast::ExpressionKind::StructuredAssignmentPattern:
        n_elems = r->as<slang::ast::StructuredAssignmentPatternExpression>().elements().size();
        break;
      default: break;
    }
    if (n_elems > 0) {
      const auto& ct       = sym.getType().getCanonicalType();
      size_t      n_fields = 0;
      if (ct.isStruct()) {
        for ([[maybe_unused]] const auto& f : ct.as<slang::ast::PackedStructType>().membersOfType<slang::ast::FieldSymbol>()) {
          ++n_fields;
        }
      }
      if (n_elems == n_fields) {  // assign_struct_whole's pattern branch will split per leaf
        absl::flat_hash_set<const slang::ast::ValueSymbol*> visiting;
        ok = driver_reads_target(*drv, sym, visiting, 0);
      }
    }
  }
  selfref_pattern_cache_.emplace(&sym, ok);
  return ok;
}

bool Slang_context::is_scalar_struct_var(const slang::ast::ValueSymbol& sym) const {
  // Ports are already flat (CIRCT/firtool flattens struct ports to scalars), and
  // a clocked struct keeps the existing flat-reg-bus path; only a comb/wire/mut
  // scalar packed struct becomes a per-field bundle.
  if (input_syms_.contains(&sym) || output_syms_.contains(&sym) || reg_syms_.contains(&sym)) {
    return false;
  }
  const auto& ct = sym.getType().getCanonicalType();
  if (!(ct.isStruct() && ct.isIntegral())) {  // packed struct (unpacked structs are non-integral)
    return false;
  }
  // A struct whose whole-copy is dropped in the per-field bundle path (a field that
  // is a nested struct / array-of-struct) must stay a consistent FLAT bus — but only
  // when it is actually WHOLE-COPIED, deep-WRITTEN, or deep-accessed through an ARRAY
  // dimension (the cases the bundle path cannot route; Dispatcher's
  // `io_out_0_bits_ctrl_0 = io_in_bits_ctrl_0` became all-zero). Scoping to those
  // vars avoids flattening every such struct in a struct-heavy design.
  //
  // A plain (struct-free) packed ARRAY field — e.g. CIRCT's io-bundle
  // `logic [1:0][5:0] enq` — is fine as a leaf: its whole value is just its flat
  // bit width, same as a scalar field (field_type_is_struct_free allows it). And a
  // plain (non-array) NESTED STRUCT field is ALSO fine when the struct is only
  // deep-READ (not whole-copied / deep-written): a read `io.sub.x` routes through
  // the leaf net `io.sub` via the generic MemberAccess path, so `sub` gets its own
  // independent leaf and the false self-loop is broken (small_todo_working.md Type
  // B — a field `c` computed from a sibling `io.sub.x` in the same `'{...}` pattern
  // no longer reads the stale whole-`io` bus). Only an ARRAY-shaped non-struct-free
  // field (array-of-struct, ElementSelect on the leaf) still forces the flat bus.
  //
  // Whole-copy and deep-write keep the STRICT rule (any nested/array field ->
  // flat): a whole-copy of a nested-struct leaf and a nested-field read-modify-write
  // both need the flat whole-struct net. EXCEPTION: a whole-copied (not
  // deep-written) struct whose whole-net driver is a SELF-REFERENCING '{...}'
  // pattern (CIRCT's `_out_output` idiom — privState built from inputs,
  // mstatus/vsstatus from _out_output.privState.*) would make the flat bus a
  // FALSE combinational loop (Type C): the fields are pairwise acyclic, only
  // the whole-net granularity is cyclic. Keep it a bundle — the pattern splits
  // one element per leaf (assign_struct_whole), deep reads route through the
  // covering leaf (Type B), and the whole copy reassembles from the leaves.
  if (struct_whole_copied_.contains(&sym) || struct_deep_written_.contains(&sym)) {
    for (const auto& f : ct.as<slang::ast::PackedStructType>().membersOfType<slang::ast::FieldSymbol>()) {
      if (!field_type_is_struct_free(f.getType())) {
        if (struct_deep_written_.contains(&sym) || !whole_copied_selfref_pattern(sym)) {
          return false;  // nested struct / array-of-struct field
        }
        break;  // self-ref pattern: bundle-safe, flat would be a false loop
      }
    }
  }
  if (struct_deep_accessed_.contains(&sym)) {
    for (const auto& f : ct.as<slang::ast::PackedStructType>().membersOfType<slang::ast::FieldSymbol>()) {
      if (field_forces_flat_bus(f.getType())) {
        // KNOWN OPEN (the DataModule__16entry / Entries* SIM comb-loop
        // cluster): extending the whole-copy branch's self-ref-pattern
        // exception here (CIRCT's register-file io idiom — `io = '{raddr,
        // rdata: <cone reading io.wdata/io.wen>, ...}` with an array-of-struct
        // wdata) fixes the standalone module but at Backend scale leaves a
        // bundle whose field store upass.tolg cannot lower ("tuple/field
        // store to 'io_enq' has no hardware lowering"). Fix order: tolg's
        // multi-element bundle store lowering first, then re-apply
        //   if (!whole_copied_selfref_pattern(sym)) return false; break;
        return false;  // array-of-struct field (a nested struct field is bundle-safe)
      }
    }
  }
  return true;
}

const Slang_context::Struct_info::Field* Slang_context::find_struct_field(const Struct_info& si,
                                                                          std::string_view  name) const {
  for (const auto& f : si.fields) {
    if (f.name == name) {
      return &f;
    }
  }
  return nullptr;
}

void Slang_context::collect_struct_pattern_assigns(const slang::ast::Scope& scope) {
  using slang::ast::ExpressionKind;
  for (const auto& member : scope.members()) {
    if (member.kind == slang::ast::SymbolKind::ContinuousAssign) {
      const auto& as = member.as<slang::ast::ContinuousAssignSymbol>().getAssignment();
      if (as.kind != ExpressionKind::Assignment) {
        continue;
      }
      const auto&                   ae  = as.as<slang::ast::AssignmentExpression>();
      const slang::ast::Expression* lhs = &ae.left();
      while (lhs->kind == ExpressionKind::Conversion) {
        lhs = &lhs->as<slang::ast::ConversionExpression>().operand();
      }
      const slang::ast::Expression* rhs = &ae.right();
      while (rhs->kind == ExpressionKind::Conversion) {
        rhs = &rhs->as<slang::ast::ConversionExpression>().operand();
      }
      const bool is_pat = rhs->kind == ExpressionKind::SimpleAssignmentPattern
                          || rhs->kind == ExpressionKind::StructuredAssignmentPattern
                          || rhs->kind == ExpressionKind::ReplicatedAssignmentPattern;
      if (is_pat && lhs->kind == ExpressionKind::NamedValue) {
        struct_pattern_assigned_.insert(&lhs->as<slang::ast::NamedValueExpression>().symbol);
      }
    } else if (member.kind == slang::ast::SymbolKind::GenerateBlock) {
      const auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
      if (!gen.isUninstantiated) {
        collect_struct_pattern_assigns(gen);
      }
    } else if (member.kind == slang::ast::SymbolKind::GenerateBlockArray) {
      for (const auto* entry : member.as<slang::ast::GenerateBlockArraySymbol>().entries) {
        collect_struct_pattern_assigns(*entry);
      }
    }
  }
}

void Slang_context::declare_struct_leaves(const slang::ast::ValueSymbol& sym) {
  const auto& st = sym.getType().getCanonicalType().as<slang::ast::PackedStructType>();
  Struct_info si;
  si.is_wire = wire_syms_.contains(&sym);
  // A REAL tuple only when cyclic (wire) AND every field is scalar: upass.detuple
  // cannot split a NESTED struct field (it defers the whole bundle), so a struct
  // with a struct-typed field keeps the flat-leaf form (the field accesses then
  // bit-slice the nested leaf, as before).
  bool all_scalar = true;
  for (const auto& f : st.membersOfType<slang::ast::FieldSymbol>()) {
    if (f.getType().getCanonicalType().isStruct()) {
      all_scalar = false;
      break;
    }
  }
  // A real tuple only when it is cyclic (wire), all-scalar, AND per-field driven
  // (a `'{...}` pattern assignment) — so detuple can split it. An instance-output
  // net or a whole-expression-driven struct is not pattern-assigned and stays flat.
  si.is_tuple = si.is_wire && all_scalar && struct_pattern_assigned_.contains(&sym);
  auto  base = lname_of(sym);
  auto& ln   = *builder_.lnast;
  set_pending_loc(sym.location);
  // A cyclic (wire) struct is emitted as a REAL tuple — `declare(io, prim_type_none,
  // wire)` + a dotted `type_spec(io.field)` per field — which upass.detuple splits
  // into per-field wire leaf nets (field reads/writes are tuple_get / field-store
  // ops). This carries the bundle/struct info through the IR (the LNAST dump and the
  // re-emitted Pyrope both show `io:(operation:u5, …)`) AND routes the slang→lg path
  // through the SAME detuple split the prp_writer's re-emitted bundle takes, so the
  // two are structurally identical for LEC. A NON-cyclic (mut) struct keeps the
  // per-field flat-leaf form (detuple leaves `mut` tuples to constprop).
  if (si.is_tuple) {
    auto d = builder_.add_child(Lnast_ntype::create_declare());
    ln.add_child(d, Lnast_node::create_ref(base));
    ln.add_child(d, Lnast_ntype::create_prim_type_none());
    ln.add_child(d, Lnast_node::create_const("wire"));
  }
  for (const auto& f : st.membersOfType<slang::ast::FieldSymbol>()) {
    auto fi = tinfo(f.getType());
    si.fields.push_back({std::string(f.name), static_cast<int64_t>(f.bitOffset), fi.bits, fi.is_signed});
    if (si.is_tuple) {
      // Dotted field type_spec (detuple reads these for the field layout).
      auto ts = builder_.add_child(Lnast_ntype::create_type_spec());
      ln.add_child(ts, Lnast_node::create_ref(absl::StrCat(base, ".", std::string(f.name))));
      emit_prim_type_int(ts, fi.bits, fi.is_signed);
    } else if (si.is_wire) {
      // A cyclic-net leaf (NOT a tuple — e.g. a nested struct): declared `wire`
      // so a forward read binds to the resolved net.
      auto leaf = absl::StrCat(base, ".", std::string(f.name));
      builder_.create_declare_stmts(leaf, "wire", int_max_str(fi.bits, fi.is_signed), int_min_str(fi.bits, fi.is_signed));
    } else {
      // A non-cyclic leaf: a `mut` net with a poison init so a read of a
      // never-assigned field is x, not a silent 0 (mirrors the scalar path).
      auto leaf = absl::StrCat(base, ".", std::string(f.name));
      builder_.create_declare_stmts(leaf, "mut", "", "");
      if (fi.is_signed) {
        builder_.create_assign_stmts(leaf, "0sb?");
      } else {
        std::string qmarks(static_cast<size_t>(fi.bits), '?');
        builder_.create_assign_stmts(leaf, absl::StrCat("0ub", qmarks));
      }
    }
  }
  clear_pending_loc();
  struct_var_info_.emplace(&sym, std::move(si));
}

// A field WRITE of a wire-tuple struct: `store(io, 'field', value)` (the detuple
// field-store shape, rewritten to `store(io.field, value)`).
void Slang_context::emit_struct_field_set(const std::string& base, const std::string& field, const std::string& value) {
  auto& ln = *builder_.lnast;
  auto  st = builder_.add_child(Lnast_ntype::create_store());
  ln.add_child(st, Lnast_node::create_ref(base));
  ln.add_child(st, Lnast_node::create_const(field));
  builder_.add_value_child_pub(st, value);
}

// A field READ of a wire-tuple struct: `tuple_get(tmp, io, 'field')` → temp (the
// detuple field-read shape, rewritten to `tmp = io.field`).
std::string Slang_context::read_struct_field_get(const std::string& base, const std::string& field) {
  auto& ln  = *builder_.lnast;
  auto  tg  = builder_.add_child(Lnast_ntype::create_tuple_get());
  auto  tmp = builder_.create_lnast_tmp();
  ln.add_child(tg, Lnast_node::create_ref(tmp));
  ln.add_child(tg, Lnast_node::create_ref(base));
  ln.add_child(tg, Lnast_node::create_const(field));
  return tmp;
}

void Slang_context::emit_leaf_store(const std::string& leaf, const std::string& value) {
  auto& ln = *builder_.lnast;
  auto  st = builder_.add_child(Lnast_ntype::create_store());
  ln.add_child(st, Lnast_node::create_ref(leaf));
  builder_.add_value_child_pub(st, value);
}

std::string Slang_context::read_leaf(const std::string& leaf) {
  // Copy the leaf net into a fresh `%` temp via a raw 2-child store(tmp, ref leaf)
  // — the same read shape the SSA port-flatten emits. The temp is a plain
  // single-level name, so a consumer (op operand OR `create_assign_stmts`) never
  // re-splits the dotted leaf path.
  auto& ln  = *builder_.lnast;
  auto  tmp = builder_.create_lnast_tmp();
  auto  st  = builder_.add_child(Lnast_ntype::create_store());
  ln.add_child(st, Lnast_node::create_ref(tmp));
  ln.add_child(st, Lnast_node::create_ref(leaf));
  return tmp;
}

std::string Slang_context::read_struct_whole(const slang::ast::ValueSymbol& sym) {
  // Reconstruct the packed value from the per-field leaves (the inverse of the
  // whole-struct write decomposition): OR each leaf, shifted to its bit offset.
  if (!declared_.contains(&sym) && !input_syms_.contains(&sym)) {
    declare_value_symbol(sym, /*force_reg=*/false);
  }
  auto it = struct_var_info_.find(&sym);
  if (it == struct_var_info_.end()) {
    return "0";
  }
  auto        base     = lname_of(sym);
  const bool  is_tuple = it->second.is_tuple;
  std::string acc;
  for (const auto& f : it->second.fields) {
    // Read each field per the struct's representation (tuple_get for a real
    // tuple, flat leaf copy otherwise), then place it at its bit offset.
    auto raw    = is_tuple ? read_struct_field_get(base, f.name) : read_leaf(absl::StrCat(base, ".", f.name));
    auto placed = to_pattern(raw, f.bits, f.is_signed);
    if (f.off != 0) {
      placed = builder_.create_shl_stmts(placed, std::to_string(f.off));
    }
    acc = acc.empty() ? placed : builder_.create_bit_or_stmts({acc, placed});
  }
  return acc.empty() ? std::string{"0"} : acc;
}

namespace {

// Per-driver def/use collection for the dependency sort. Reads are every
// NamedValue in rvalue position plus partial-write LHS bases (the RMW
// lowering reads them); full scalar-write LHS bases are NOT reads.
struct Dep_collector : public slang::ast::ASTVisitor<Dep_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*> reads;
  absl::flat_hash_set<const slang::ast::ValueSymbol*> writes;

  void note_lhs(const slang::ast::Expression& lhs) {
    switch (lhs.kind) {
      case ExpressionKind::NamedValue:
      case ExpressionKind::HierarchicalValue: writes.insert(&lhs.as<slang::ast::ValueExpressionBase>().symbol); return;
      case ExpressionKind::Conversion: note_lhs(lhs.as<slang::ast::ConversionExpression>().operand()); return;
      case ExpressionKind::Concatenation:
        for (const auto* op : lhs.as<slang::ast::ConcatenationExpression>().operands()) {
          note_lhs(*op);
        }
        return;
      case ExpressionKind::ElementSelect: {
        const auto& es = lhs.as<slang::ast::ElementSelectExpression>();
        es.selector().visit(*this);
        if (const auto* sym = lhs_base_symbol(es.value())) {
          writes.insert(sym);
          reads.insert(sym);  // partial write reads the base (RMW)
        }
        return;
      }
      case ExpressionKind::RangeSelect: {
        const auto& rs = lhs.as<slang::ast::RangeSelectExpression>();
        rs.left().visit(*this);
        rs.right().visit(*this);
        if (const auto* sym = lhs_base_symbol(rs.value())) {
          writes.insert(sym);
          reads.insert(sym);
        }
        return;
      }
      case ExpressionKind::MemberAccess: {
        if (const auto* sym = lhs_base_symbol(lhs)) {
          writes.insert(sym);
          reads.insert(sym);
        }
        return;
      }
      default:
        if (const auto* sym = lhs_base_symbol(lhs)) {
          writes.insert(sym);
          reads.insert(sym);
        }
    }
  }

  void handle(const slang::ast::AssignmentExpression& expr) {
    note_lhs(expr.left());
    expr.right().visit(*this);
    if (expr.timingControl != nullptr) {
      expr.timingControl->visit(*this);
    }
  }

  void handle(const slang::ast::ValueExpressionBase& expr) { reads.insert(&expr.symbol); }
};

}  // namespace

// External nets connected to an instance's PURELY-REGISTERED output ports — an
// output whose internal driver is a submodule register q (so it is
// combinationally independent of every input, exactly like a flop q at this
// level). A read of such a net is order-free: it must NOT make the reader depend
// on the instance. Otherwise a pipeline-register feedback — a forward path reads
// `stage_reg.out`, whose `stage_reg.in = f(forwarded)` — is mistaken for a comb
// loop, and the cyclic-net `wire` fallback then routes the (now position-
// independent) read through the wrong driver (the forwarding mux), miscompiling
// every consumer of that stage register's output (e.g. io_dmem_address reading a
// don't-care instead of the ex/mem result).
static void collect_registered_outputs(const slang::ast::InstanceSymbol&               inst,
                                        absl::flat_hash_set<const slang::ast::Symbol*>& ext_nets) {
  // The INSTANCE's own body (NOT getCanonicalBody) so the reg set computed below
  // and this instance's port.internalSymbol reference the same per-instance
  // symbols. With the canonical (first-instance) body, every non-first instance
  // — e.g. pipeB_* mirroring pipeA_* — would find no port match and keep its
  // false comb-loop back-edges.
  const auto* body = &inst.body;

  // (1) submodule reg set: vars written nonblocking in an edge-sensitive always.
  absl::flat_hash_set<const slang::ast::Symbol*> regs;
  for (const auto& member : body->members()) {
    if (member.kind != SymbolKind::ProceduralBlock) {
      continue;
    }
    const auto& pbs     = member.as<slang::ast::ProceduralBlockSymbol>();
    bool        is_edge = false;
    if (pbs.procedureKind == slang::ast::ProceduralBlockKind::Always
        || pbs.procedureKind == slang::ast::ProceduralBlockKind::AlwaysFF) {
      const auto& stmt = pbs.getBody();
      if (stmt.kind == StatementKind::Timed) {
        const auto& timing = stmt.as<slang::ast::TimedStatement>().timing;
        auto        scan   = [&](const slang::ast::TimingControl& tc) {
          if (tc.kind == slang::ast::TimingControlKind::SignalEvent) {
            auto edge = tc.as<slang::ast::SignalEventControl>().edge;
            is_edge |= edge == slang::ast::EdgeKind::PosEdge || edge == slang::ast::EdgeKind::NegEdge;
          }
        };
        if (timing.kind == slang::ast::TimingControlKind::EventList) {
          for (const auto* ev : timing.as<slang::ast::EventListControl>().events) {
            scan(*ev);
          }
        } else {
          scan(timing);
        }
      }
    }
    if (!is_edge) {
      continue;
    }
    Write_collector wc;
    pbs.getBody().visit(wc);
    for (const auto* s : wc.nonblocking) {
      regs.insert(s);
    }
  }

  // (2) internal output nets that are a pure reg q: the net IS a reg, or is
  //     driven by exactly `assign net = reg_q` (a bare reference, modulo casts).
  absl::flat_hash_set<const slang::ast::Symbol*> reg_driven;
  for (const auto& member : body->members()) {
    if (member.kind != SymbolKind::ContinuousAssign) {
      continue;
    }
    const auto& as = member.as<slang::ast::ContinuousAssignSymbol>().getAssignment();
    if (as.kind != ExpressionKind::Assignment) {
      continue;
    }
    const auto& ae  = as.as<slang::ast::AssignmentExpression>();
    const auto* lhs = lhs_base_symbol(ae.left());
    if (lhs == nullptr) {
      continue;
    }
    const slang::ast::Expression* rhs = &ae.right();
    while (rhs->kind == ExpressionKind::Conversion) {
      rhs = &rhs->as<slang::ast::ConversionExpression>().operand();
    }
    if (rhs->kind == ExpressionKind::NamedValue && regs.contains(&rhs->as<slang::ast::ValueExpressionBase>().symbol)) {
      reg_driven.insert(lhs);
    }
  }

  // (3) mark the external net of every output port whose internal symbol is a
  //     pure reg q. Conservative: any output we cannot prove registered keeps
  //     its combinational dependency (the prior behavior), so this never breaks
  //     a real comb path — it only removes false back-edges.
  for (const auto* conn : inst.getPortConnections()) {
    const auto* expr = conn->getExpression();
    if (expr == nullptr || conn->port.kind != SymbolKind::Port) {
      continue;
    }
    const auto& port = conn->port.as<slang::ast::PortSymbol>();
    if (port.direction != slang::ast::ArgumentDirection::Out || port.internalSymbol == nullptr) {
      continue;
    }
    if (!regs.contains(port.internalSymbol) && !reg_driven.contains(port.internalSymbol)) {
      continue;
    }
    const auto* target = expr;
    if (const auto* assign = target->as_if<slang::ast::AssignmentExpression>()) {
      target = &assign->left();
    }
    if (const auto* sym = lhs_base_symbol(*target)) {
      ext_nets.insert(sym);
    }
  }
}

void Slang_context::lower_members(const slang::ast::Scope& scope) {
  // ── pass 1: collect drivers (recursing through generate blocks) ───────────
  struct Driver {
    const slang::ast::Symbol*                           member;
    std::string                                         prefix;  // genblk name prefix at collection point
    absl::flat_hash_set<const slang::ast::ValueSymbol*> reads;
    absl::flat_hash_set<const slang::ast::ValueSymbol*> writes;
  };
  std::vector<Driver> drivers;
  // External nets driven by purely-registered instance outputs; reads of them are
  // order-free (see collect_registered_outputs) so pass 2 skips their back-edges.
  absl::flat_hash_set<const slang::ast::Symbol*> seq_out_nets;

  std::function<void(const slang::ast::Scope&)> collect = [&](const slang::ast::Scope& sc) {
    for (const auto& member : sc.members()) {
      switch (member.kind) {
        case SymbolKind::Port:
        case SymbolKind::Parameter:
        case SymbolKind::TypeParameter:
        case SymbolKind::TypeAlias:
        case SymbolKind::TransparentMember:
        case SymbolKind::EmptyMember:
        case SymbolKind::Genvar:
        case SymbolKind::StatementBlock:    // lowered where referenced (slang puts them next to procedures)
        case SymbolKind::Subroutine:        // bodies fold at call sites or are diagnosed there
        case SymbolKind::ElabSystemTask:    // $info/$warning/$error handled by slang itself
        case SymbolKind::WildcardImport:    // `import pkg::*` — slang already resolved the names
        case SymbolKind::ExplicitImport:    // `import pkg::sym` — ditto
        case SymbolKind::Modport:           // interface modport view; not codegen-relevant here
        case SymbolKind::AssertionPort:     // property/sequence formal args
        case SymbolKind::Sequence:          // named sequences (assertion-only, not synthesized)
        case SymbolKind::Property:          // named properties (assertion-only, not synthesized)
          break;

        case SymbolKind::Net: {
          const auto& ns = member.as<slang::ast::NetSymbol>();
          if (const auto* expr = ns.getInitializer()) {
            Driver d{&member, genblk_prefix_, {}, {}};
            Dep_collector dc;
            expr->visit(dc);
            d.reads = std::move(dc.reads);
            d.writes.insert(&ns);
            drivers.push_back(std::move(d));
          }
          break;
        }

        case SymbolKind::Variable: {
          const auto& vs = member.as<slang::ast::VariableSymbol>();
          if (vs.getInitializer() != nullptr) {
            emit_warning(slang::SourceRange(vs.location, vs.location), "var-init-ignored", "unsupported",
                         std::string("initializer of '") + std::string(vs.name) + "' is ignored (initial-block semantics)");
          }
          break;
        }

        case SymbolKind::ContinuousAssign: {
          Driver        d{&member, genblk_prefix_, {}, {}};
          Dep_collector dc;
          member.as<slang::ast::ContinuousAssignSymbol>().getAssignment().visit(dc);
          d.reads  = std::move(dc.reads);
          d.writes = std::move(dc.writes);
          drivers.push_back(std::move(d));
          break;
        }

        case SymbolKind::ProceduralBlock: {
          Driver        d{&member, genblk_prefix_, {}, {}};
          Dep_collector dc;
          member.as<slang::ast::ProceduralBlockSymbol>().getBody().visit(dc);
          d.reads  = std::move(dc.reads);
          d.writes = std::move(dc.writes);
          drivers.push_back(std::move(d));
          break;
        }

        case SymbolKind::Instance: {
          collect_registered_outputs(member.as<slang::ast::InstanceSymbol>(), seq_out_nets);
          Driver d{&member, genblk_prefix_, {}, {}};
          for (const auto* conn : member.as<slang::ast::InstanceSymbol>().getPortConnections()) {
            const auto* expr = conn->getExpression();
            if (expr == nullptr || conn->port.kind != SymbolKind::Port) {
              continue;
            }
            Dep_collector dc;
            if (conn->port.as<slang::ast::PortSymbol>().direction == slang::ast::ArgumentDirection::Out) {
              const auto* target = expr;
              if (const auto* assign = target->as_if<slang::ast::AssignmentExpression>()) {
                target = &assign->left();
              }
              dc.note_lhs(*target);
            } else {
              expr->visit(dc);
            }
            d.reads.insert(dc.reads.begin(), dc.reads.end());
            d.writes.insert(dc.writes.begin(), dc.writes.end());
          }
          drivers.push_back(std::move(d));
          break;
        }

        case SymbolKind::GenerateBlock: {
          const auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
          if (gen.isUninstantiated) {
            break;
          }
          auto saved_prefix = genblk_prefix_;
          if (!gen.name.empty() || gen.getParentScope()->asSymbol().kind != SymbolKind::GenerateBlockArray) {
            genblk_prefix_ = absl::StrCat(genblk_prefix_, gen.getExternalName(), "_");
          }
          collect(gen);
          genblk_prefix_ = saved_prefix;
          break;
        }

        case SymbolKind::GenerateBlockArray: {
          const auto& arr          = member.as<slang::ast::GenerateBlockArraySymbol>();
          auto        saved_prefix = genblk_prefix_;
          for (const auto* entry : arr.entries) {
            std::string idx_txt
                = entry->arrayIndex != nullptr ? entry->arrayIndex->toString() : std::to_string(entry->constructIndex);
            genblk_prefix_ = absl::StrCat(saved_prefix, arr.getExternalName(), "_", idx_txt, "_");
            collect(*entry);
          }
          genblk_prefix_ = saved_prefix;
          break;
        }

        case SymbolKind::UninstantiatedDef:
          emit_unsupported(member.location, "unknown-module",
                           std::string("instance '") + std::string(member.name)
                               + "' refers to an unknown module (blackboxes are not supported by --reader slang)",
                           "provide the module source, or use --reader yosys-slang with a blackbox library");
          break;

        default:
          emit_unsupported(member.location, "unsupported-member",
                           std::string("module member '") + std::string(member.name) + "' (kind "
                               + std::string(slang::ast::toString(member.kind)) + ") is not supported by --reader slang");
      }
    }
  };
  collect(scope);

  // ── pass 2: dependency edges. A read of a wire depends on every driver
  // writing it; reads of regs (q pins), inputs, and locals are order-free.
  // Co-writers of one wire (partial writers) keep source order instead of an
  // edge (the RMW chain is order-stable and a cycle otherwise).
  absl::flat_hash_map<const slang::ast::ValueSymbol*, std::vector<size_t>> writers_of;
  for (size_t i = 0; i < drivers.size(); ++i) {
    for (const auto* w : drivers[i].writes) {
      writers_of[w].push_back(i);
    }
  }
  std::vector<absl::flat_hash_set<size_t>> deps(drivers.size());
  for (size_t i = 0; i < drivers.size(); ++i) {
    for (const auto* r : drivers[i].reads) {
      if (reg_syms_.contains(r) || input_syms_.contains(r) || seq_out_nets.contains(r)) {
        continue;
      }
      auto it = writers_of.find(r);
      if (it == writers_of.end()) {
        continue;
      }
      const bool i_writes_r = drivers[i].writes.contains(r);
      for (size_t w : it->second) {
        if (w == i || i_writes_r) {
          continue;
        }
        deps[i].insert(w);
      }
    }
  }

  // ── pass 3: stable Kahn (ready drivers emit in source order) ──────────────
  std::vector<size_t> order;
  std::vector<bool>   emitted(drivers.size(), false);
  bool                progress = true;
  while (progress) {
    progress = false;
    for (size_t i = 0; i < drivers.size(); ++i) {
      if (emitted[i]) {
        continue;
      }
      bool ready = true;
      for (size_t d : deps[i]) {
        if (!emitted[d]) {
          ready = false;
          break;
        }
      }
      if (ready) {
        order.push_back(i);
        emitted[i] = true;
        progress   = true;
      }
    }
  }
  std::vector<size_t> cyclic;
  for (size_t i = 0; i < drivers.size(); ++i) {
    if (!emitted[i]) {
      cyclic.push_back(i);
    }
  }
  if (!cyclic.empty()) {
    // emit_warning(slang::SourceRange(drivers[cyclic.front()].member->location, drivers[cyclic.front()].member->location),
    //              "comb-loop", "time",
    //              "combinational dependency cycle between drivers; emitting cyclic nets as `wire` (position-independent reads)");
    // 2c-wire — every net written by a cyclic driver is declared a `wire` so a
    // read before its driver binds to the resolved net (position-independent),
    // replacing the old `.[defer]` end-of-cycle read. A reg/input is never a
    // wire (its read is already order-free).
    for (size_t i : cyclic) {
      for (const auto* w : drivers[i].writes) {
        if (w == nullptr || reg_syms_.contains(w) || input_syms_.contains(w)) {
          continue;
        }
        // Only a MODULE-LEVEL net becomes a `wire` (position-independent). A
        // procedural-block-local var has no stable cut driver and needs its mut
        // poison-init; declaring it `wire` would drop the init and miscompile.
        const auto* sc           = w->getParentScope();
        const bool  module_level = sc != nullptr
                                   && (&sc->asSymbol() == body_
                                       || sc->asSymbol().kind == slang::ast::SymbolKind::GenerateBlock);
        if (module_level) {
          wire_syms_.insert(w);
        }
      }
    }
  }

  // A registered instance output (seq_out_nets) is order-free, so pass 2 dropped
  // its back-edges — but that also drops the "instance before reader" ordering, so
  // a reader may now precede the instance. Declare these nets `wire` too, so such a
  // forward read binds to the resolved net (position-independent) exactly like the
  // cyclic-net wires above — without which the read resolves to nil (e.g. the pc's
  // forward read of `_pipeB_ex_mem_io_data_nextpc` froze the PC at 0).
  for (const auto* w : seq_out_nets) {
    const auto* sc = w->getParentScope();
    if (sc != nullptr && (&sc->asSymbol() == body_ || sc->asSymbol().kind == slang::ast::SymbolKind::GenerateBlock)) {
      wire_syms_.insert(w);
    }
  }


  // ── pass 4: emit ──────────────────────────────────────────────────────────
  // Cyclic drivers emit last (after the topologically-ordered ones); their nets
  // are declared `wire` (2c-wire), so a forward read binds to the resolved net.
  auto emit_driver = [&](size_t i) {
    const auto& d            = drivers[i];
    auto        saved_prefix = genblk_prefix_;
    genblk_prefix_           = d.prefix;
    switch (d.member->kind) {
      case SymbolKind::Net: {
        const auto& ns = d.member->as<slang::ast::NetSymbol>();
        proc_kind_     = Proc_kind::none;
        set_pending_loc(ns.getInitializer()->sourceRange);
        if (!declared_.contains(&ns) && !input_syms_.contains(&ns)) {
          declare_value_symbol(ns, false);
        }
        // A scalar-struct-var net declares per-field leaves and ALL its reads
        // (whole or by-field) go through those leaves, but the whole-net initializer
        // never wrote them -> reads resolve to nil (collect_struct_pattern_assigns
        // only scans ContinuousAssign, missing the initializer form). Split the
        // initializer into per-field writes like the continuous-`assign x = '{...}`
        // path — ALWAYS when the var is bundle-declared: an old narrower gate
        // (all-scalar or read-by-field) left a NESTED struct that is read only
        // WHOLE on the flat-store path, but a whole read of a scalar-struct-var
        // ALSO resolves through the leaves, so that flat store was a dual
        // identity the reads never saw (CtrlBlock's
        // `_GEN_3923 = '{...cfVec[7].bits...}` consumed by a whole-concat:
        // decl-only `= nil` in the emitted Pyrope -> "incompletely driven").
        // Nested fields are flattened scalar leaves since the Type-B bundle
        // work, so leaf<->whole round-trips cleanly for them too.
        if (const char* dbg = std::getenv("SLANG_DUMP_NETINIT");
            dbg != nullptr && ns.name.find(dbg) != std::string_view::npos) {
          std::fprintf(stderr,
                       "[SLANG_NETINIT] '%s' scalar_struct=%d all_scalar=%d field_read=%d deep_access=%d "
                       "whole_copied=%d deep_written=%d\n",
                       std::string(ns.name).c_str(), is_scalar_struct_var(ns) ? 1 : 0, struct_is_all_scalar(ns) ? 1 : 0,
                       struct_field_read_.contains(&ns) ? 1 : 0, struct_deep_accessed_.contains(&ns) ? 1 : 0,
                       struct_whole_copied_.contains(&ns) ? 1 : 0, struct_deep_written_.contains(&ns) ? 1 : 0);
        }
        if (is_scalar_struct_var(ns)
            && (std::getenv("SLANG_NETINIT_OLDGATE") == nullptr || struct_is_all_scalar(ns)
                || struct_field_read_.contains(&ns))) {
          current_assign_nonblocking_ = false;
          if (assign_struct_whole(ns, *ns.getInitializer())) {
            clear_pending_loc();
            break;
          }
        }
        // A net is an integer lvalue, so a bool initializer (e.g. `wire x =
        // a == b;`) materializes to 0/1 — the same rule lower_assign applies to
        // the continuous-`assign` path.
        auto v = to_int_value(lower_rvalue(*ns.getInitializer()));
        builder_.create_assign_stmts(lname_of(ns), v);
        clear_pending_loc();
        break;
      }
      case SymbolKind::ContinuousAssign: lower_continuous_assign(d.member->as<slang::ast::ContinuousAssignSymbol>()); break;
      case SymbolKind::ProceduralBlock: lower_process(d.member->as<slang::ast::ProceduralBlockSymbol>()); break;
      case SymbolKind::Instance: lower_instance(d.member->as<slang::ast::InstanceSymbol>()); break;
      default: break;
    }
    genblk_prefix_ = saved_prefix;
  };

  for (size_t i : order) {
    emit_driver(i);
  }
  for (size_t i : cyclic) {
    emit_driver(i);
  }
}

void Slang_context::lower_continuous_assign(const slang::ast::ContinuousAssignSymbol& ca) {
  proc_kind_ = Proc_kind::none;
  proc_assign_style_.clear();
  proc_blocking_written_.clear();

  if (ca.getDelay() != nullptr) {
    emit_warning(slang::SourceRange(ca.location, ca.location), "delay-ignored", "unsupported", "assignment delay is ignored (synthesis semantics)");
  }

  const auto& as = ca.getAssignment();
  if (as.kind != ExpressionKind::Assignment) {
    emit_unsupported(ca.location, "unsupported-continuous-assign", "unsupported continuous assignment shape");
    return;
  }
  set_pending_loc(as.sourceRange);
  lower_assign(as.as<slang::ast::AssignmentExpression>());
  clear_pending_loc();
}

// Canonical reset-name test, token-aware to avoid matching "first"/"burst"
// (mirrors pass/lec/query.cpp reset_name_polarity; polarity is irrelevant here
// because a demoted reset is READ by the body, not wired to a reset pin).
static bool is_reset_like_name(std::string_view nm) {
  std::string lc(nm);
  for (auto& c : lc) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  size_t start = 0;
  for (size_t i = 0; i <= lc.size(); ++i) {
    if (i == lc.size() || lc[i] == '_') {
      std::string_view tok = std::string_view(lc).substr(start, i - start);
      if (tok == "rst" || tok == "reset" || tok == "rstn" || tok == "resetn" || tok == "arst" || tok == "areset"
          || tok == "nrst" || tok == "nreset" || tok == "por") {
        return true;
      }
      start = i + 1;
    }
  }
  return false;
}

void Slang_context::lower_process(const slang::ast::ProceduralBlockSymbol& pbs) {
  using slang::ast::ProceduralBlockKind;
  using slang::ast::TimingControlKind;

  proc_assign_style_.clear();
  proc_blocking_written_.clear();
  unroll_budget_ = options_.unroll_limit;

  switch (pbs.procedureKind) {
    case ProceduralBlockKind::Initial:
      // Synthesis ignores initial blocks (memory init is a 2s-D follow-up).
      emit_warning(slang::SourceRange(pbs.location, pbs.location), "initial-ignored", "unsupported", "initial block is ignored (synthesis semantics)");
      return;
    case ProceduralBlockKind::Final: return;
    case ProceduralBlockKind::AlwaysComb: lower_comb_process(pbs.getBody()); return;
    case ProceduralBlockKind::AlwaysLatch:
      // The latch state vars were classified in collect_state_vars (declared as
      // mode "latch" → Ntype_op::Latch). The body lowers like any if/store; the
      // store rebinds the latch's din/enable shadows (tolg lower_if branch-merge
      // gives din = cond?d:q, enable = cond), exactly as for a reg.
      lower_comb_process(pbs.getBody());
      return;
    case ProceduralBlockKind::Always:
    case ProceduralBlockKind::AlwaysFF: break;
  }

  const auto& stmt = pbs.getBody();
  // A standalone `assert/assume/cover property(...)` is modeled by slang as an
  // implicit Always procedural block whose body is the assertion (possibly
  // wrapped in a Block/List), NOT a Timed event-control. Assertions are not
  // synthesized, so ignore such bodies (mirrors lower_statement in slang_stmt.cpp).
  std::function<bool(const slang::ast::Statement&)> assertion_only = [&](const slang::ast::Statement& s) -> bool {
    switch (s.kind) {
      case StatementKind::Empty:
      case StatementKind::ImmediateAssertion:
      case StatementKind::ConcurrentAssertion: return true;
      case StatementKind::Block: return assertion_only(s.as<slang::ast::BlockStatement>().body);
      case StatementKind::List:
        for (const auto* c : s.as<slang::ast::StatementList>().list) {
          if (!assertion_only(*c)) {
            return false;
          }
        }
        return true;
      default: return false;
    }
  };
  if (stmt.kind != StatementKind::Timed && assertion_only(stmt)) {
    emit_warning(stmt.sourceRange, "assertion-ignored", "unsupported", "assertion-only process ignored (synthesis semantics)");
    return;
  }
  if (stmt.kind != StatementKind::Timed) {
    emit_unsupported(stmt.sourceRange, "unsupported-always",
                     "always block without an event control is not supported by --reader slang");
    return;
  }
  const auto& timed = stmt.as<slang::ast::TimedStatement>();

  std::vector<const slang::ast::SignalEventControl*> edges;
  bool                                               implicit = false;
  bool                                               bad      = false;

  std::function<void(const slang::ast::TimingControl&)> classify = [&](const slang::ast::TimingControl& tc) {
    switch (tc.kind) {
      case TimingControlKind::ImplicitEvent: implicit = true; break;
      case TimingControlKind::SignalEvent: {
        const auto& se = tc.as<slang::ast::SignalEventControl>();
        if (se.iffCondition != nullptr) {
          emit_unsupported(tc.sourceRange, "unsupported-iff", "iff event qualifiers are not supported");
          bad = true;
          return;
        }
        switch (se.edge) {
          case slang::ast::EdgeKind::None: implicit = true; break;  // @(a or b) sensitivity-list style
          case slang::ast::EdgeKind::PosEdge:
          case slang::ast::EdgeKind::NegEdge: edges.push_back(&se); break;
          case slang::ast::EdgeKind::BothEdges:
            emit_unsupported(tc.sourceRange, "unsupported-dual-edge", "dual-edge @(edge x) is not supported");
            bad = true;
            break;
        }
        break;
      }
      case TimingControlKind::EventList:
        for (const auto* ev : tc.as<slang::ast::EventListControl>().events) {
          classify(*ev);
        }
        break;
      default:
        emit_unsupported(tc.sourceRange, "unsupported-timing", "this event control is not supported by --reader slang");
        bad = true;
    }
  };
  classify(timed.timing);

  if (bad) {
    return;
  }
  if (implicit && !edges.empty()) {
    emit_unsupported(timed.timing.sourceRange, "edge-implicit-mixing",
                     "mixing edge and non-edge sensitivity in one always block is not supported");
    return;
  }
  if (implicit || edges.empty()) {
    lower_comb_process(timed.stmt);
    return;
  }

  // Edge-sensitive: extract the async-reset rungs (yosys-slang
  // interpret_async_pattern) until one clock trigger remains. Each extra
  // edge trigger must guard the next if/else rung; its then-arm holds the
  // CONST reset values, which become per-reg initial/reset_pin/sync attrs.
  std::vector<const slang::ast::Statement*> prologue;
  const slang::ast::Statement*              body      = &timed.stmt;
  bool                                      peeled_any = false;  // a rung already extracted?

  // Fallback when a rung cannot be extracted (compound guard `if (rst || soft)`,
  // missing else arm, non-const async load, ...): LEC and lhd sim observe state
  // only AFTER a clock update, so an edge-triggered reset is indistinguishable
  // from a synchronous one — drop the reset's edge trigger and lower the body as
  // ordinary clocked logic that READS the signal (yosys async2sync semantics).
  //
  // This is sound ONLY under three gates (each guards a confirmed miscompile):
  //  (1) NO rung has peeled yet. A peeled reset is no longer present in the
  //      residual body, so lowering that body synchronously would drop it — a reg
  //      reset by the peeled rung but written in the residual body would update
  //      while the async reset is held high at a clock edge.
  //  (2) every demoted signal is READABLE here (module input or module-level /
  //      generate net). A non-readable reset lowers to a driverless poison-init
  //      whose guard folds to a constant, silently killing the reset arm.
  //  (3) every demoted signal is actually REFERENCED in the residual body, so the
  //      synchronous lowering re-derives the reset AND the surviving edge is
  //      genuinely the clock. This refuses a reset-token-named CLOCK (`clk_por`):
  //      the clock is not read as data in the body, so demoting it (which would
  //      move the flop onto the guard signal's domain) is rejected.
  // Only reset-named triggers demote (an unrecognized second edge stays a hard
  // error), and exactly one trigger — the clock — must remain.
  auto demote_reset_edges = [&]() -> bool {
    if (peeled_any) {
      return false;  // gate (1)
    }
    std::vector<size_t> demote;
    for (size_t i = 0; i < edges.size(); ++i) {
      if (edges[i]->expr.kind == ExpressionKind::NamedValue
          && is_reset_like_name(edges[i]->expr.as<slang::ast::NamedValueExpression>().symbol.name)) {
        demote.push_back(i);
      }
    }
    if (demote.empty() || edges.size() - demote.size() != 1) {
      return false;  // need exactly one surviving edge (the clock)
    }

    Named_value_collector body_reads;
    body->visit(body_reads);
    absl::flat_hash_set<const slang::ast::ValueSymbol*> read_set(body_reads.syms.begin(), body_reads.syms.end());

    auto is_readable = [&](const slang::ast::ValueSymbol* sym) {
      if (input_syms_.contains(sym)) {
        return true;
      }
      const auto* rsc = sym->getParentScope();
      return rsc != nullptr
             && (&rsc->asSymbol() == body_ || rsc->asSymbol().kind == slang::ast::SymbolKind::GenerateBlock);
    };
    for (size_t idx : demote) {
      const auto* sym = &edges[idx]->expr.as<slang::ast::NamedValueExpression>().symbol;
      if (!is_readable(sym) || !read_set.contains(sym)) {
        return false;  // gates (2) + (3)
      }
    }
    // A demoted module-level net (not an input) must be declared so the
    // synchronous body can read its driver.
    for (size_t idx : demote) {
      const auto* sym = &edges[idx]->expr.as<slang::ast::NamedValueExpression>().symbol;
      if (!input_syms_.contains(sym) && !declared_.contains(sym)) {
        declare_value_symbol(*sym, /*force_reg=*/false);
      }
    }
    for (auto it = demote.rbegin(); it != demote.rend(); ++it) {
      const auto& sym = edges[*it]->expr.as<slang::ast::NamedValueExpression>().symbol;
      emit_warning(edges[*it]->sourceRange, "async-reset-as-sync", "time",
                   std::string("edge-triggered reset '") + std::string(sym.name)
                       + "' has no extractable async-reset rung; modeled as a synchronous reset (state is only observed after clock updates)");
      edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    return true;
  };

  while (edges.size() > 1) {
    if (body->kind == StatementKind::Block) {
      body = &body->as<slang::ast::BlockStatement>().body;
      continue;
    }
    // A named block may carry local declarations before the reset if/else
    // (`begin : p int errs; if (!rst) ... end`). Hoist the declarations into the
    // prologue (lowered with the clocked body) and descend into the lone
    // conditional, so the reset-rung extraction still sees `if (rst) ... else`.
    if (body->kind == StatementKind::List) {
      const slang::ast::Statement* cond = nullptr;
      bool                         ok   = true;
      std::vector<const slang::ast::Statement*> pre;
      for (const auto* sub : body->as<slang::ast::StatementList>().list) {
        if (sub->kind == StatementKind::Empty) {
          continue;
        }
        if (sub->kind == StatementKind::VariableDeclaration) {
          pre.push_back(sub);
        } else if (sub->kind == StatementKind::Conditional && cond == nullptr) {
          cond = sub;
        } else {
          ok = false;
          break;
        }
      }
      if (ok && cond != nullptr) {
        for (const auto* p : pre) {
          prologue.push_back(p);
        }
        body = cond;
        continue;
      }
    }
    if (body->kind != StatementKind::Conditional) {
      if (demote_reset_edges()) {
        break;
      }
      emit_unsupported(body->sourceRange, "unsupported-async-pattern",
                       "expected `if (rst) ... else ...` rungs for the extra edge triggers",
                       "use a synchronous reset, or --reader yosys-slang");
      return;
    }
    const auto& cond_stmt = body->as<slang::ast::ConditionalStatement>();
    if (cond_stmt.conditions.size() != 1 || cond_stmt.conditions[0].pattern != nullptr || cond_stmt.ifFalse == nullptr) {
      if (demote_reset_edges()) {
        break;
      }
      emit_unsupported(body->sourceRange, "unsupported-async-pattern",
                       "the async-reset if must have one plain condition and an else arm");
      return;
    }

    // Normalize the condition to (signal symbol, polarity).
    const slang::ast::Expression* cond     = cond_stmt.conditions[0].expr;
    bool                          polarity = true;
    while (true) {
      if (cond->kind == ExpressionKind::UnaryOp) {
        const auto& un = cond->as<slang::ast::UnaryExpression>();
        if (un.op == slang::ast::UnaryOperator::LogicalNot || un.op == slang::ast::UnaryOperator::BitwiseNot) {
          polarity = !polarity;
          cond     = &un.operand();
          continue;
        }
      }
      if (cond->kind == ExpressionKind::Conversion) {
        cond = &cond->as<slang::ast::ConversionExpression>().operand();
        continue;
      }
      if (cond->kind == ExpressionKind::BinaryOp) {
        const auto& bin = cond->as<slang::ast::BinaryExpression>();
        if (bin.op == slang::ast::BinaryOperator::Equality || bin.op == slang::ast::BinaryOperator::Inequality) {
          if (auto cv = try_eval(bin.right()); cv && cv->isInteger()) {
            const bool rhs_true = cv->isTrue();
            if (bin.op == slang::ast::BinaryOperator::Inequality ? rhs_true : !rhs_true) {
              polarity = !polarity;
            }
            cond = &bin.left();
            continue;
          }
        }
      }
      break;
    }
    if (cond->kind != ExpressionKind::NamedValue) {
      if (demote_reset_edges()) {
        break;
      }
      emit_unsupported(cond->sourceRange, "unsupported-async-pattern", "the async-reset condition must be a plain signal");
      return;
    }
    const auto* rst_sym = &cond->as<slang::ast::NamedValueExpression>().symbol;

    // Match the condition signal to one of the extra edge triggers.
    size_t match = edges.size();
    for (size_t i = 0; i < edges.size(); ++i) {
      if (edges[i]->expr.kind == ExpressionKind::NamedValue
          && &edges[i]->expr.as<slang::ast::NamedValueExpression>().symbol == rst_sym) {
        match = i;
        break;
      }
    }
    if (match == edges.size()) {
      if (demote_reset_edges()) {
        break;
      }
      emit_unsupported(cond->sourceRange, "unsupported-async-pattern",
                       "the async-reset condition does not name one of the edge triggers");
      return;
    }
    const bool edge_pos = edges[match]->edge == slang::ast::EdgeKind::PosEdge;
    if (edge_pos != polarity) {
      emit_warning(cond->sourceRange, "async-reset-polarity", "time",
                   "the async-reset guard polarity does not match its edge trigger");
    }
    // A reset synchronizer drives the flop from a DERIVED module-level signal
    // (rst_int_ni = scanmode ? scan_reset_n : rst_ni), not a module input.
    // Accept an input OR a module-level net/var (tolg resolves its driver and
    // wires it to reset_pin). A local/block-scoped signal still has no stable
    // cut driver -> reject.
    if (!input_syms_.contains(rst_sym)) {
      const auto* rsc          = rst_sym->getParentScope();
      const bool  module_level = rsc != nullptr
                                && (&rsc->asSymbol() == body_
                                    || rsc->asSymbol().kind == slang::ast::SymbolKind::GenerateBlock);
      if (!module_level) {
        if (demote_reset_edges()) {
          break;
        }
        emit_unsupported(cond->sourceRange, "unsupported-async-pattern",
                         "the async reset must be a module input or a module-level signal");
        return;
      }
      if (!declared_.contains(rst_sym)) {
        declare_value_symbol(*rst_sym, /*force_reg=*/false);
      }
    }

    // The then-arm must be const nonblocking stores to regs: those become
    // the reset values. Validate-and-collect first, emit after — a partial
    // emit would leave stray reset attrs behind when a later statement fails
    // the walk and the demote fallback lowers the arm synchronously instead.
    std::vector<std::pair<const slang::ast::ValueSymbol*, std::string>> reset_stores;
    std::function<bool(const slang::ast::Statement&)> harvest = [&](const slang::ast::Statement& s) -> bool {
      switch (s.kind) {
        case StatementKind::Empty: return true;
        case StatementKind::Block: return harvest(s.as<slang::ast::BlockStatement>().body);
        case StatementKind::List: {
          for (const auto* sub : s.as<slang::ast::StatementList>().list) {
            if (!harvest(*sub)) {
              return false;
            }
          }
          return true;
        }
        case StatementKind::Conditional: {
          // Reset arms commonly guard config-dependent regs with a compile-time
          // `if` (e.g. `if (CVA6Cfg.RVZCMT) ...`, `if (FPGA_ALTERA) ...`). Fold
          // the constant condition and harvest only the taken branch.
          const auto& cs = s.as<slang::ast::ConditionalStatement>();
          if (cs.conditions.size() != 1 || cs.conditions[0].pattern != nullptr) {
            return false;
          }
          auto cv = try_eval(*cs.conditions[0].expr);
          if (!cv || !cv->isInteger()) {
            return false;  // a non-constant reset guard has no flop lowering
          }
          if (cv->isTrue()) {
            return harvest(cs.ifTrue);
          }
          return cs.ifFalse == nullptr ? true : harvest(*cs.ifFalse);
        }
        case StatementKind::ExpressionStatement: {
          const auto& e = s.as<slang::ast::ExpressionStatement>().expr;
          if (e.kind != ExpressionKind::Assignment) {
            return false;
          }
          const auto& as  = e.as<slang::ast::AssignmentExpression>();
          const auto* sym = lhs_base_symbol(as.left());
          if (sym == nullptr || as.left().kind != ExpressionKind::NamedValue || !reg_syms_.contains(sym)) {
            return false;
          }
          auto cv = try_eval_const_net(as.right());
          if (!cv || !cv->isInteger()) {
            return false;
          }
          reset_stores.emplace_back(sym, const_text(cv->integer()));
          return true;
        }
        default: return false;
      }
    };
    if (!harvest(cond_stmt.ifTrue)) {
      if (demote_reset_edges()) {
        break;
      }
      emit_unsupported(cond_stmt.ifTrue.sourceRange, "unsupported-async-load",
                       "the async-reset arm must contain only constant non-blocking writes to state regs",
                       "non-constant async loads have no flop lowering; use --reader yosys-slang");
      return;
    }
    for (const auto& [sym, init_text] : reset_stores) {
      declare_reg(*sym);  // ensure declared (hoisting normally did)
      auto  name = lname_of(*sym);
      auto& ln   = *builder_.lnast;
      auto  emit_attr = [&](std::string_view key, const std::string& val, bool val_is_ref) {
        auto idx = builder_.add_child(Lnast_ntype::create_attr_set());
        ln.add_child(idx, Lnast_node::create_ref(name));
        ln.add_child(idx, Lnast_node::create_const(key));
        if (val_is_ref) {
          ln.add_child(idx, Lnast_node::create_ref(val));
        } else {
          ln.add_child(idx, Lnast_node::create_const(val));
        }
      };
      emit_attr("initial", init_text, false);
      emit_attr("reset_pin", lname_of(*rst_sym), true);
      emit_attr("sync", "false", false);
      if (!edge_pos) {
        emit_attr("negreset", "true", false);
      }
    }

    edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(match));
    body       = cond_stmt.ifFalse;
    peeled_any = true;  // a demote after this point would drop this reset — gate (1)
  }

  lower_ff_process(*edges[0], *body, prologue);
}

void Slang_context::lower_comb_process(const slang::ast::Statement& body) {
  proc_kind_ = Proc_kind::comb;
  lower_statement(body);
  proc_kind_ = Proc_kind::none;
}

void Slang_context::lower_ff_process(const slang::ast::SignalEventControl& clock, const slang::ast::Statement& body,
                                     std::vector<const slang::ast::Statement*>& prologue) {
  proc_kind_ = Proc_kind::seq;

  // Identify the clock signal; tolg reuses a declared 1-bit `clk`/`clock`
  // input implicitly. Other names/edges ride per-reg attrs after the body
  // (clock_pin / posclk), keyed on the regs this process writes.
  const slang::ast::ValueSymbol* clk_sym = nullptr;
  if (clock.expr.kind == ExpressionKind::NamedValue) {
    clk_sym = &clock.expr.as<slang::ast::NamedValueExpression>().symbol;
  }
  if (clk_sym == nullptr) {
    emit_unsupported(clock.sourceRange, "unsupported-clock", "the clock must be a plain signal");
    proc_kind_ = Proc_kind::none;
    return;
  }
  const bool negedge      = clock.edge == slang::ast::EdgeKind::NegEdge;
  const bool implicit_clk = !negedge && (clk_sym->name == "clk" || clk_sym->name == "clock");

  Write_collector wc;
  body.visit(wc);

  // A blocking-written variable of an edge process that other code reads has
  // flop semantics this reader does not model yet.
  for (const auto* sym : wc.blocking) {
    if (output_syms_.contains(sym)) {
      emit_unsupported(sym->location, "blocking-ff-output",
                       std::string("output '") + std::string(sym->name)
                           + "' is blocking-assigned in an edge-sensitive process; --reader slang only supports `<=` for state",
                       "use a non-blocking assignment");
    }
  }

  for (const auto* stmt : prologue) {
    lower_statement(*stmt);
  }
  lower_statement(body);

  if (!implicit_clk) {
    auto& ln = *builder_.lnast;
    // Emit the clock_pin / posclk attr_set nodes in a stable source order:
    // wc.nonblocking is a pointer-keyed flat_hash_set with run-to-run-varying
    // iteration order, and this loop appends IR.
    std::vector<const slang::ast::ValueSymbol*> ff_syms(wc.nonblocking.begin(), wc.nonblocking.end());
    std::sort(ff_syms.begin(), ff_syms.end(), [](const slang::ast::ValueSymbol* a, const slang::ast::ValueSymbol* b) {
      if (a->location != b->location) {
        return a->location < b->location;
      }
      return a->name < b->name;
    });
    for (const auto* sym : ff_syms) {
      if (!reg_syms_.contains(sym)) {
        continue;
      }
      auto name = lname_of(*sym);
      if (!(clk_sym->name == "clk" || clk_sym->name == "clock")) {
        auto idx = builder_.add_child(Lnast_ntype::create_attr_set());
        ln.add_child(idx, Lnast_node::create_ref(name));
        ln.add_child(idx, Lnast_node::create_const("clock_pin"));
        ln.add_child(idx, Lnast_node::create_ref(lname_of(*clk_sym)));
      }
      if (negedge) {
        auto idx = builder_.add_child(Lnast_ntype::create_attr_set());
        ln.add_child(idx, Lnast_node::create_ref(name));
        ln.add_child(idx, Lnast_node::create_const("posclk"));
        ln.add_child(idx, Lnast_node::create_const("false"));
      }
    }
  }

  proc_kind_ = Proc_kind::none;
}

Slang_context::Tinfo Slang_context::flat_or_tinfo(const slang::ast::Type& t) {
  const auto& ct = t.getCanonicalType();
  if (!ct.isIntegral() && ct.kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
    const auto& arr  = ct.as<slang::ast::FixedSizeUnpackedArrayType>();
    const auto& elem = arr.elementType.getCanonicalType();
    if (elem.isIntegral() && !elem.isUnpackedArray()) {
      Tinfo r;
      r.bits      = tinfo(elem).bits * static_cast<int>(arr.range.width());
      r.is_signed = false;
      return r;
    }
  }
  return tinfo(t);
}

void Slang_context::lower_instance(const slang::ast::InstanceSymbol& inst) {
  // 2c-wire — a cyclic handshake's forward reference to a not-yet-lowered net is
  // handled by declaring that net a `wire` (position-independent reads), so no
  // transient cycle flag needs preserving across the recursive lower_module.
  if (!lower_module(inst)) {
    return;  // diagnosed inside
  }
  auto callee = module_name_of(inst);

  proc_kind_ = Proc_kind::none;

  // Lower input connections first, then emit the func_call with named args,
  // then bind outputs. tolg resolves the callee by module name in the
  // registry and emits an Ntype_op::Sub.
  struct Out_conn {
    const slang::ast::PortSymbol* port;
    const slang::ast::Expression* expr;
  };
  std::vector<std::pair<std::string, std::string>> in_args;  // (port, value)
  std::vector<Out_conn>                            outs;
  size_t                                           n_outputs_total = 0;

  for (const auto* conn : inst.getPortConnections()) {
    if (conn->port.kind != SymbolKind::Port) {
      emit_unsupported(inst.location, "unsupported-port-conn",
                       std::string("instance '") + std::string(inst.name) + "' connects an unsupported port kind");
      return;
    }
    const auto& port   = conn->port.as<slang::ast::PortSymbol>();
    const auto* expr   = conn->getExpression();
    const bool  is_out = port.direction == slang::ast::ArgumentDirection::Out;
    if (port.direction == slang::ast::ArgumentDirection::InOut || port.direction == slang::ast::ArgumentDirection::Ref) {
      emit_unsupported(inst.location, "unsupported-inout-port",
                       std::string("instance '") + std::string(inst.name) + "' connects inout port '" + std::string(port.name)
                           + "'");
      return;
    }
    if (is_out) {
      ++n_outputs_total;
    }

    if (expr == nullptr) {  // unconnected
      if (!is_out) {
        auto ti = flat_or_tinfo(port.getType());
        // unconnected input reads x
        std::string qmarks(static_cast<size_t>(ti.bits), '?');
        in_args.emplace_back(std::string(port.name), absl::StrCat("0ub", qmarks));
      }
      continue;
    }

    if (is_out) {
      // slang wraps output connections as `<expr> = EmptyArgument`.
      if (const auto* assign = expr->as_if<slang::ast::AssignmentExpression>()) {
        expr = &assign->left();
      }
      outs.push_back({&port, expr});
    } else {
      set_pending_loc(expr->sourceRange);
      auto v  = to_int_value(lower_rvalue(*expr));
      auto pi = flat_or_tinfo(port.getType());
      auto ei = flat_or_tinfo(*expr->type);
      v       = materialize_conversion(v, ei.bits, ei.is_signed, pi.bits, pi.is_signed);
      clear_pending_loc();
      in_args.emplace_back(std::string(port.name), v);
    }
  }

  auto& ln = *builder_.lnast;
  set_pending_loc(inst.location);
  auto fcall_idx = builder_.add_child(Lnast_ntype::create_func_call());
  // Use the RTL instance name (id_ex, if_id, …) as the call result ref so the
  // tolg Sub instance gets named after the instance (mirrors Pyrope, where the
  // call dst is the instance name). This is what `get_hier_name()` reports as
  // the Verilog-style hierarchy component. Fall back to a temp for an unnamed
  // instance.
  auto result = inst.name.empty() ? builder_.create_lnast_tmp() : std::string(inst.name);
  ln.add_child(fcall_idx, Lnast_node::create_ref(result));
  ln.add_child(fcall_idx, Lnast_node::create_ref(callee));
  for (const auto& [pname, v] : in_args) {
    auto arg = ln.add_child(fcall_idx, Lnast_ntype::create_store());
    ln.add_child(arg, Lnast_node::create_ref(pname));
    builder_.add_value_child_pub(arg, v);
  }

  // Bind outputs: a single-output callee's result is the value itself;
  // multi-output callees yield a tuple read by field name.
  const bool single_out = n_outputs_total == 1;
  for (const auto& oc : outs) {
    std::string v;
    if (single_out) {
      v = result;
    } else {
      auto tg = builder_.add_child(Lnast_ntype::create_tuple_get());
      auto t  = builder_.create_lnast_tmp();
      ln.add_child(tg, Lnast_node::create_ref(t));
      ln.add_child(tg, Lnast_node::create_ref(result));
      ln.add_child(tg, Lnast_node::create_const(oc.port->name));
      v = t;
    }
    auto pi = flat_or_tinfo(oc.port->getType());
    auto ei = flat_or_tinfo(*oc.expr->type);
    // An output port bound to a bundle-declared struct var (ExeUnitImp_4/
    // VSetRiWvf `_vsetModule_io_out`) is split onto the per-field leaves
    // inside assign_to (assign_struct_whole_value) — a flat store would be
    // dead (reads resolve through the leaves; the .prp text round-trip used
    // to lose the flat↔leaf relation, and a nested-struct var dropped the
    // binding entirely). Guard: prp-v2prp/prp-simfail-instance_out_struct_ident.
    assign_to(*oc.expr, materialize_conversion(v, pi.bits, pi.is_signed, ei.bits, ei.is_signed));
  }
  clear_pending_loc();
}
