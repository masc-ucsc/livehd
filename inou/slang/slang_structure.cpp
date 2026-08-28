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
#include <type_traits>
#include <vector>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Lookup.h"
#include "slang/ast/Statement.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/AssertionExpr.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/syntax/AllSyntax.h"
#include "slang_context.hpp"

using slang::ast::ExpressionKind;
using slang::ast::StatementKind;
using slang::ast::SymbolKind;

namespace {

// A STABLE TOTAL ORDER over AST symbols — for every place this reader has to
// linearize a pointer-keyed container before appending IR.
//
// `(location, name)` is NOT a total order. A generate-loop body is elaborated
// once per index, so every replica of a symbol declared INSIDE it shares both
// keys: the comparator then reports each pair equivalent and `std::sort` keeps
// whatever order the `flat_hash_set` handed it, which abseil perturbs by the
// table's heap address. That leaked straight into the output. Two back-to-back
// `--emit-dir pyrope:` runs over minion produced 22 DIFFERING files, and
// because `lname_of`'s `_sN` uniquing numbers names in first-reference order,
// the differences were not only statement order but SIGNAL IDENTITY:
// `intpipe_csr_msgs` came out pairing `oob_mem` with `oob_wr_ptr_s6` in one run
// and with `_s7` in the next. A checked-in Pyrope tree cannot be LEC'd against
// a freshly compiled Verilog reference under that: the two sides' state names
// permute and the miter compares the wrong replicas (the `nxt:oob_rd_ptr_sN`
// counterexamples that moved from run to run).
//
// The hierarchical path (`…gen_msg_ptrs_port[1].oob_rd_ptr`) is unique per
// ELABORATED symbol and identical across runs, so it separates exactly the
// symbols that tie. It is built only on a tie, so the common case pays nothing.
bool sym_emit_less(const slang::ast::Symbol* a, const slang::ast::Symbol* b) {
  if (a == b) {
    return false;
  }
  if (a->location != b->location) {
    return a->location < b->location;
  }
  if (a->name != b->name) {
    return a->name < b->name;
  }
  return a->getHierarchicalPath() < b->getHierarchicalPath();
}

// Sorted (sym_emit_less) copy of a pointer-keyed container — the KEYS for a
// map, the elements for a set. Every loop in this reader that iterates a
// `flat_hash_set`/`flat_hash_map` of AST symbols to APPEND IR has to go through
// this: abseil's iteration order varies with the table's heap address, and the
// name uniquing downstream turns that into differing output (see sym_emit_less).
// One helper rather than the copy/sort spelled out at each site, so the next
// container that needs linearizing cannot get a subtly different version.
template <class C>
auto emit_ordered(const C& c) {
  using Elem = std::remove_cvref_t<decltype(*c.begin())>;
  if constexpr (requires(const Elem& e) { e.first; }) {
    std::vector<std::remove_cvref_t<decltype(c.begin()->first)>> v;
    v.reserve(c.size());
    for (const auto& e : c) {
      v.push_back(e.first);
    }
    std::sort(v.begin(), v.end(), sym_emit_less);
    return v;
  } else {
    std::vector<Elem> v(c.begin(), c.end());
    std::sort(v.begin(), v.end(), sym_emit_less);
    return v;
  }
}

// True iff `type` bottoms out in plain bits with no STRUCT anywhere in its
// shape (recurses through any number of packed/unpacked array dimensions).
// Used to gate the per-field leaf-wire bundle split: a field that is a plain
// multi-dimensional array (e.g. CIRCT's `logic [1:0][5:0] enq`) is fine — its
// whole value is just `getBitWidth()` flat bits, same as a scalar field — but
// a (possibly array-of-) nested struct field needs its own recursive split,
// which the bundle path does not do (see is_scalar_struct_var).
bool field_type_is_struct_free(const slang::ast::Type& type) {
  const auto& ct = type.getCanonicalType();
  // A packed UNION counts like a struct: a deep write `x.u.member = v` needs
  // the flat whole-struct net to root its RMW exactly as a nested struct does
  // (rooting on the detupled base wrote an undeclared flat net nobody reads).
  if (ct.isStruct() || ct.isPackedUnion()) {
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
      case ExpressionKind::ElementSelect    : e = &e->as<slang::ast::ElementSelectExpression>().value(); break;
      case ExpressionKind::RangeSelect      : e = &e->as<slang::ast::RangeSelectExpression>().value(); break;
      case ExpressionKind::MemberAccess     : e = &e->as<slang::ast::MemberAccessExpression>().value(); break;
      case ExpressionKind::Conversion       : e = &e->as<slang::ast::ConversionExpression>().operand(); break;
      case ExpressionKind::Concatenation    : return nullptr;  // caller iterates operands itself
      default                               : return nullptr;
    }
  }
}

// The real expression of an unknown-module (UninstantiatedDef) port
// connection. slang binds these against the ERROR type (there is no port to
// type them), wrapping the bound expression in an InvalidExpression — unwrap
// to the self-determined child. Returns nullptr for an unconnected `.p()`
// (EmptyArgument) or a non-simple (assertion-shaped) connection.
const slang::ast::Expression* unknown_conn_expr(const slang::ast::AssertionExpr* conn) {
  if (conn == nullptr) {
    return nullptr;
  }
  const auto* sae = conn->as_if<slang::ast::SimpleAssertionExpr>();
  if (sae == nullptr) {
    return nullptr;
  }
  const auto* e = &sae->expr;
  while (e != nullptr && e->kind == ExpressionKind::Invalid) {
    e = e->as<slang::ast::InvalidExpression>().child;
  }
  if (e == nullptr || e->kind == ExpressionKind::EmptyArgument) {
    return nullptr;
  }
  return e;
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

// Blocking writes of a process, EXCLUDING for-loop control (`for (j = 0; j <
// N; j = j+1)`). A module-scope `integer j` used as a loop index in several
// processes is not hardware state -- elaboration unrolls the loop -- but it IS
// blocking-written in an edge process and referenced by the others, which made
// every such design look like a silently-dropped register. Used only by
// collect_blocking_ff_state; the plain Write_collector still sees everything.
struct Ff_blocking_collector : public slang::ast::ASTVisitor<Ff_blocking_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*> blocking;

  void handle(const slang::ast::ForLoopStatement& s) { s.body.visit(*this); }  // skip init / stop / step

  void handle(const slang::ast::AssignmentExpression& expr) {
    if (!expr.isNonBlocking()) {
      std::function<void(const slang::ast::Expression&)> note = [&](const slang::ast::Expression& lhs) {
        if (lhs.kind == ExpressionKind::Concatenation) {
          for (const auto* op : lhs.as<slang::ast::ConcatenationExpression>().operands()) {
            note(*op);
          }
          return;
        }
        if (const auto* sym = lhs_base_symbol(lhs)) {
          blocking.insert(sym);
        }
      };
      note(expr.left());
    }
    visitDefault(expr);
  }
};

// Symbols DEFINITELY assigned (blocking) on EVERY path through `stmt`.
//
// This is what decides an INFERRED LATCH: in a level-sensitive process, a
// variable that some path leaves unwritten RETAINS ITS VALUE, which is a latch —
// `always @(*) if (en) q = d;` is the canonical form, and every synthesis tool
// infers a `$dlatch` from it. Without this analysis the reader classified such a
// block as pure combinational logic and emitted `q = en ? d : X`, dropping the
// hold path entirely: measured on inou/prp/tests/equiv/latch_enable_hold.v,
// whose round-trip came back as a stateless `comb`.
//
// BIASED TOWARD COMBINATIONAL on anything not precisely modelled. The two errors
// are not symmetric: calling comb logic a latch INSERTS state that the source
// never had (a new, silent miscompile), while missing a latch reproduces today's
// behaviour. So an unhandled statement kind reports every write beneath it as
// definite, and only the shapes below — an `if` with no `else`, a `case` with no
// `default` — actually produce a latch.
//
// `strict` INVERTS that bias for a caller that needs a proof rather than a
// guess: an unmodelled statement kind then reports NOTHING. The wire promotion
// in lower_members is such a caller — there an over-report turns a
// CONDITIONALLY stored net into a single-driver `wire`, the wrong direction.
// `for (int i…) if (sel==i) s = …;` is the shape that separates the two modes:
// the loop hits the `default` arm below, so the permissive mode calls that one
// store definite even though `sel` may match no iteration and `s` then holds.
// Neither mode models a `disable`-escape, the only control transfer that can
// skip a write inside a modelled shape (`break`/`continue` need a loop, which is
// unmodelled anyway, and `return` needs a function body, never descended into).
void definite_blocking_writes(const slang::ast::Statement& stmt, absl::flat_hash_set<const slang::ast::ValueSymbol*>& out,
                              bool strict = false) {
  using slang::ast::StatementKind;
  auto all_writes_below = [&](const slang::ast::Statement& s) {
    Write_collector wc;
    s.visit(wc);
    out.insert(wc.blocking.begin(), wc.blocking.end());
  };
  auto intersect_into = [&](const std::vector<const slang::ast::Statement*>& arms) {
    if (arms.empty()) {
      return;
    }
    absl::flat_hash_set<const slang::ast::ValueSymbol*> acc;
    definite_blocking_writes(*arms.front(), acc, strict);
    for (size_t i = 1; i < arms.size(); ++i) {
      absl::flat_hash_set<const slang::ast::ValueSymbol*> other;
      definite_blocking_writes(*arms[i], other, strict);
      absl::flat_hash_set<const slang::ast::ValueSymbol*> keep;
      for (const auto* sym : acc) {
        if (other.contains(sym)) {
          keep.insert(sym);
        }
      }
      acc = std::move(keep);
    }
    out.insert(acc.begin(), acc.end());
  };

  switch (stmt.kind) {
    case StatementKind::Empty: return;
    case StatementKind::List:
      for (const auto* s : stmt.as<slang::ast::StatementList>().list) {
        definite_blocking_writes(*s, out, strict);  // sequential: a later write still counts
      }
      return;
    case StatementKind::Block              : definite_blocking_writes(stmt.as<slang::ast::BlockStatement>().body, out, strict); return;
    case StatementKind::Timed              : definite_blocking_writes(stmt.as<slang::ast::TimedStatement>().stmt, out, strict); return;
    case StatementKind::ExpressionStatement: {
      const auto& e = stmt.as<slang::ast::ExpressionStatement>().expr;
      if (e.kind != ExpressionKind::Assignment) {
        return;
      }
      const auto& a = e.as<slang::ast::AssignmentExpression>();
      if (a.isNonBlocking()) {
        return;  // a nonblocking write in a level-sensitive block is the M1 latch idiom
      }
      std::function<void(const slang::ast::Expression&)> note = [&](const slang::ast::Expression& lhs) {
        if (lhs.kind == ExpressionKind::Concatenation) {
          for (const auto* op : lhs.as<slang::ast::ConcatenationExpression>().operands()) {
            note(*op);
          }
          return;
        }
        if (const auto* sym = lhs_base_symbol(lhs)) {
          out.insert(sym);
        }
      };
      note(a.left());
      return;
    }
    case StatementKind::Conditional: {
      const auto& c = stmt.as<slang::ast::ConditionalStatement>();
      if (c.ifFalse == nullptr) {
        return;  // THE latch shape: the un-taken path holds the previous value
      }
      intersect_into({&c.ifTrue, c.ifFalse});
      return;
    }
    case StatementKind::Case: {
      const auto& c = stmt.as<slang::ast::CaseStatement>();
      if (c.defaultCase == nullptr) {
        // No default => an unmatched selector holds. A `unique`/`priority`
        // qualifier is a CLAIM about the selector, not a proof of coverage, so it
        // is deliberately not treated as one here.
        return;
      }
      std::vector<const slang::ast::Statement*> arms{c.defaultCase};
      for (const auto& item : c.items) {
        arms.push_back(item.stmt);
      }
      intersect_into(arms);
      return;
    }
    default:
      if (strict) {
        return;  // no proof this runs at all (a loop may iterate zero times)
      }
      all_writes_below(stmt);
      return;  // bias to combinational (see above)
  }
}

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

// Every WHOLE-symbol assignment (`sym <= rhs`, the LHS is the bare name) with
// its right-hand side. Used to spot a packed-array reg that is loaded with a
// PER-ELEMENT constant pattern — the async-reset shape firtool emits for an
// index-initialized pointer vector:
//
//   if (reset) deqPtrVec <= '{'{flag:0,value:7}, … '{flag:0,value:0}};
//
// A reset value rides a SCALAR `initial` attr, and on `reg x:[N]uW` a scalar
// initial BROADCASTS to every entry, so such an array cannot be spelled as a
// Pyrope array yet (see lower_members).
struct Whole_store_collector : public slang::ast::ASTVisitor<Whole_store_collector, slang::ast::VisitFlags::AllGood> {
  std::vector<std::pair<const slang::ast::ValueSymbol*, const slang::ast::Expression*>> stores;
  // Every symbol written at all, whole or per element. A reset arm that loads
  // an array one entry at a time (`if (rst) arr[k] <= k;`) still gives that
  // array a reset, even though it never appears in `stores`.
  absl::flat_hash_set<const slang::ast::ValueSymbol*>                                   touched;

  void handle(const slang::ast::AssignmentExpression& a) {
    const slang::ast::Expression* l = &a.left();
    while (l->kind == ExpressionKind::Conversion) {
      l = &l->as<slang::ast::ConversionExpression>().operand();
    }
    if (l->kind == ExpressionKind::NamedValue) {
      const auto& sym = l->as<slang::ast::NamedValueExpression>().symbol;
      stores.emplace_back(&sym, &a.right());
      touched.insert(&sym);
    } else if (const auto* base = lhs_base_symbol(*l)) {
      touched.insert(base);
    }
    visitDefault(a);
  }
};

// True iff every symbol the selector references is statically resolvable once
// the reader unrolls loops: a for-loop induction var, a genvar, a parameter, or
// an enum value. A reference to any genuine runtime signal (net / port /
// register / module variable) makes the index runtime — keep that array a
// memory (dynamic-shift flattening mismatches; cf. the `tuplish` regression).
struct Static_selector_scan : public slang::ast::ASTVisitor<Static_selector_scan, slang::ast::VisitFlags::AllGood> {
  const absl::flat_hash_set<const slang::ast::Symbol*>* loop_vars  = nullptr;
  bool                                                  all_static = true;

  void handle(const slang::ast::ValueExpressionBase& e) {
    const auto k = e.symbol.kind;
    if (k != slang::ast::SymbolKind::Genvar && k != slang::ast::SymbolKind::Parameter && k != slang::ast::SymbolKind::EnumValue
        && !loop_vars->contains(&e.symbol)) {
      all_static = false;
    }
  }
};

// Collect every NamedValue leaf symbol referenced by an expression subtree.
struct Named_value_collector : public slang::ast::ASTVisitor<Named_value_collector, slang::ast::VisitFlags::AllGood> {
  std::vector<const slang::ast::ValueSymbol*> syms;
  void                                        handle(const slang::ast::NamedValueExpression& e) {
    syms.push_back(&e.symbol);
    visitDefault(e);
  }
};

// Collect the base symbol of every member-access (`x.field`) in the module, so
// the caller knows which struct nets are read by-field (vs only whole).
struct Member_read_collector : public slang::ast::ASTVisitor<Member_read_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*>* out = nullptr;
  void                                                 handle(const slang::ast::MemberAccessExpression& ma) {
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
struct Deep_struct_access_collector : public slang::ast::ASTVisitor<Deep_struct_access_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*>* out = nullptr;
  void                                                 note(const slang::ast::Expression& base) {
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
struct Deep_struct_write_collector : public slang::ast::ASTVisitor<Deep_struct_write_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*>* out = nullptr;
  // `base` is the value() the OUTERMOST lvalue select/member sits on; the write
  // is "deep" when that base is itself a member/element/range select.
  void                                                 note_deep(const slang::ast::Expression& base) {
    if (base.kind == slang::ast::ExpressionKind::MemberAccess || base.kind == slang::ast::ExpressionKind::ElementSelect
        || base.kind == slang::ast::ExpressionKind::RangeSelect) {
      if (const auto* sym = lhs_base_symbol(base)) {
        out->insert(sym);
      }
    }
  }
  void note_lhs(const slang::ast::Expression& lhs) {
    switch (lhs.kind) {
      case slang::ast::ExpressionKind::MemberAccess : note_deep(lhs.as<slang::ast::MemberAccessExpression>().value()); return;
      case slang::ast::ExpressionKind::ElementSelect: note_deep(lhs.as<slang::ast::ElementSelectExpression>().value()); return;
      case slang::ast::ExpressionKind::RangeSelect  : note_deep(lhs.as<slang::ast::RangeSelectExpression>().value()); return;
      case slang::ast::ExpressionKind::Conversion   : note_lhs(lhs.as<slang::ast::ConversionExpression>().operand()); return;
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
struct Struct_whole_copy_collector : public slang::ast::ASTVisitor<Struct_whole_copy_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*>* out = nullptr;
  void                                                 note(const slang::ast::Expression& e) {
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

// A write through a MULTI-ELEMENT range select of the whole array
// (`arr[2:1] <= v`). Only the FLAT bus representation lowers that write
// correctly -- set_mask composes it -- so such an array must be kept out of the
// packed-2D memory classifier below: a Memory has one write port per element
// store and nowhere to put `arr[2:1] <= v`, and the write is then silently
// dropped (the memory instantiates with `wr_enable_0(1'b0)`).
//
// This is all that survives of the packed-array SROA collector: the split it
// used to gate is gone, but the range-write refusal was never part of it -- it
// is a correctness bail that the memory classifier depends on.
struct Array_range_write_collector : public slang::ast::ASTVisitor<Array_range_write_collector, slang::ast::VisitFlags::AllGood> {
  // ALL THREE outputs are required. None is null-checked on purpose: a missing
  // `range_written` would silently drop a CORRECTNESS bail (the write vanishes),
  // which is strictly worse than crashing at the one call site that sets them.
  absl::flat_hash_set<const slang::ast::ValueSymbol*>*      range_written  = nullptr;
  // Packed arrays assigned as a WHOLE (`arr = <expr>` / `assign arr = '{...}`),
  // COUNTED. The Type-C self-reference check below needs the candidate list, and
  // it needs the count too: `whole_net_driver` returns the FIRST driver it
  // finds, so promoting a multiply-driven net to a single-driver Pyrope `wire`
  // would silently drop every other driver (the old SROA gate spelled this
  // `in.whole_drivers == 1`). This visitor is already walking every assignment.
  absl::flat_hash_map<const slang::ast::ValueSymbol*, int>* whole_assigned = nullptr;
  // Packed arrays written through a PART of themselves (`arr[i] = v`,
  // `arr[i].f = v`, `arr[i][3:0] = v`). A Pyrope `wire` is single-driver by
  // contract, so the Type-C repair below must refuse an array that carries such
  // a write ALONGSIDE its whole-array driver -- the old SROA gate spelled the
  // same refusal as `!elem_written`.
  absl::flat_hash_set<const slang::ast::ValueSymbol*>*      part_written   = nullptr;

  // A packed array of genuine MULTI-BIT elements (a real 2-D array like
  // `[6:0][53:0]`), not a plain bus: `logic [1:0]` is a packed array of 1-bit
  // elements too, and its range writes are ordinary bit-slice writes.
  static bool is_candidate(const slang::ast::ValueSymbol& sym) {
    const auto& ct = sym.getType().getCanonicalType();
    if (!(ct.isPackedArray() && ct.isIntegral() && ct.getArrayElementType() != nullptr)) {
      return false;
    }
    return ct.getArrayElementType()->getBitWidth() > 1;
  }

  void handle(const slang::ast::AssignmentExpression& a) {
    const slang::ast::Expression* l = &a.left();
    while (l->kind == slang::ast::ExpressionKind::Conversion) {
      l = &l->as<slang::ast::ConversionExpression>().operand();
    }
    if (l->kind == slang::ast::ExpressionKind::NamedValue) {
      const auto& sym = l->as<slang::ast::NamedValueExpression>().symbol;
      if (is_candidate(sym)) {
        ++(*whole_assigned)[&sym];
      }
    }
    if (const auto* base = lhs_base_symbol(*l); base != nullptr && is_candidate(*base)) {
      if (l->kind != slang::ast::ExpressionKind::NamedValue) {
        part_written->insert(base);
      }
      const slang::ast::Expression* part = l;
      while (part != nullptr) {
        if (part->kind == slang::ast::ExpressionKind::ElementSelect) {
          part = &part->as<slang::ast::ElementSelectExpression>().value();
          continue;
        }
        if (part->kind == slang::ast::ExpressionKind::MemberAccess) {
          part = &part->as<slang::ast::MemberAccessExpression>().value();
          continue;
        }
        if (part->kind == slang::ast::ExpressionKind::RangeSelect) {
          const auto& rs = part->as<slang::ast::RangeSelectExpression>();
          // A range select DIRECTLY on the array spans whole elements
          // (`arr[2:1]`); one nested under an element select is a sub-element
          // bit slice (`arr[i][3:0]`), which the per-element write handles.
          if (rs.value().kind == slang::ast::ExpressionKind::NamedValue && lhs_base_symbol(rs.value()) == base) {
            range_written->insert(base);
          }
          part = &rs.value();
          continue;
        }
        break;
      }
    }
    visitDefault(a);
  }
};

// Select packed-vector ports whose body repeatedly addresses constant bits.
// These are profitable leaf ports (the compressor-array xN_i[i] shape), unlike
// a vector used only as a whole value. Dynamic selects stay packed: expanding
// those would merely replace one indexed extraction with a large Hotmux.
struct Packed_vector_port_collector : public slang::ast::ASTVisitor<Packed_vector_port_collector, slang::ast::VisitFlags::AllGood> {
  struct Info {
    int  static_selects = 0;
    bool dynamic_select = false;
  };
  const absl::flat_hash_set<const slang::ast::ValueSymbol*>* ports     = nullptr;
  const absl::flat_hash_set<const slang::ast::Symbol*>*      loop_vars = nullptr;
  absl::flat_hash_map<const slang::ast::ValueSymbol*, Info>  info;

  bool is_static(const slang::ast::Expression& selector) const {
    const slang::ast::Expression* s = &selector;
    while (s->kind == slang::ast::ExpressionKind::Conversion) {
      s = &s->as<slang::ast::ConversionExpression>().operand();
    }
    if (s->kind == slang::ast::ExpressionKind::IntegerLiteral) {
      return true;
    }
    Static_selector_scan scan;
    scan.loop_vars = loop_vars;
    s->visit(scan);
    return scan.all_static;
  }

  static bool is_plain_vector(const slang::ast::ValueSymbol& sym) {
    const auto& ct = sym.getType().getCanonicalType();
    return ct.isPackedArray() && ct.isIntegral() && ct.getArrayElementType() != nullptr
           && ct.getArrayElementType()->getBitWidth() == 1 && ct.getBitWidth() > 1 && ct.getBitWidth() <= 256;
  }

  void handle(const slang::ast::ElementSelectExpression& expr) {
    if (expr.value().kind == slang::ast::ExpressionKind::NamedValue) {
      const auto& sym = expr.value().as<slang::ast::NamedValueExpression>().symbol;
      if (ports->contains(&sym) && is_plain_vector(sym)) {
        auto& use = info[&sym];
        if (is_static(expr.selector())) {
          ++use.static_selects;
        } else {
          use.dynamic_select = true;
        }
      }
    }
    visitDefault(expr);
  }

  void handle(const slang::ast::RangeSelectExpression& expr) {
    if (expr.value().kind == slang::ast::ExpressionKind::NamedValue) {
      const auto& sym = expr.value().as<slang::ast::NamedValueExpression>().symbol;
      if (ports->contains(&sym) && is_plain_vector(sym)) {
        info[&sym].dynamic_select = true;
      }
    }
    visitDefault(expr);
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

// Per-attempt state shared by every seed_const_net frame of ONE fold.
struct Seed_memo {
  // A shared non-constant cone can be reached from thousands of reset-value
  // candidates. Remembering the negative result keeps this fold linear: without
  // it a diamond-shaped datapath is traversed exponentially and every failed
  // eval grows EvalContext::Diagnostics (EnqEntry_4 reached tens of GB).
  absl::flat_hash_set<const slang::ast::ValueSymbol*> failed;
  // A leaf that could not be seeded does NOT always make its reader
  // non-constant: a conditional with a constant condition, a `$bits()` type
  // query and a replication count never evaluate the operand that failed
  // (`assign cw = ONE ? 8'hA5 : d;` used as an async-reset value). Attempt the
  // fold anyway, under a small per-fold budget so a big shared RUNTIME cone
  // still bails out quickly instead of being re-walked for every candidate.
  int  spec_evals_left = 256;
  // A refusal caused by the DEPTH cap is a property of the path, not of the
  // symbol, so it must not be memoized (the same guard graph/split_selfref
  // spells `cap_hit == cap_before`). A `visiting` cycle IS a property of the
  // symbol -- a net on a comb cycle depends on itself -- and stays memoized.
  bool depth_capped     = false;
};

bool seed_const_net(const slang::ast::ValueSymbol& sym, slang::ast::EvalContext& ctx,
                    absl::flat_hash_set<const slang::ast::ValueSymbol*>& visiting, Seed_memo& memo, int depth);

// One dependency of a constant-fold: true when it needs no seeding at all
// (already an EvalContext local, or a Parameter/EnumValue/Genvar that
// NamedValueExpression::eval resolves directly) or when seeding it worked.
bool seed_dep(const slang::ast::ValueSymbol& dep, slang::ast::EvalContext& ctx,
              absl::flat_hash_set<const slang::ast::ValueSymbol*>& visiting, Seed_memo& memo, int depth) {
  if (ctx.findLocal(&dep) != nullptr) {
    return true;
  }
  const auto kind = dep.kind;
  if (kind == slang::ast::SymbolKind::Parameter || kind == slang::ast::SymbolKind::EnumValue
      || kind == slang::ast::SymbolKind::Genvar) {
    return true;
  }
  return seed_const_net(dep, ctx, visiting, memo, depth);
}

// Seed a net/var's constant value into `ctx` (as an EvalContext local) by
// chasing its whole-net driver, recursively seeding the driver's own net
// leaves first. Best-effort: returns true if `sym` got a constant value.
bool seed_const_net(const slang::ast::ValueSymbol& sym, slang::ast::EvalContext& ctx,
                    absl::flat_hash_set<const slang::ast::ValueSymbol*>& visiting, Seed_memo& memo, int depth) {
  if (ctx.findLocal(&sym) != nullptr) {
    return true;
  }
  if (memo.failed.contains(&sym)) {
    return false;
  }
  if (depth > 64) {
    memo.depth_capped = true;
    return false;
  }
  if (!visiting.insert(&sym).second) {
    return false;  // cycle
  }
  const bool capped_before = memo.depth_capped;
  bool       ok            = false;
  if (const auto* drv = whole_net_driver(sym)) {
    Named_value_collector col;
    drv->visit(col);
    bool deps_ready = true;
    for (const auto* dep : col.syms) {
      // A runtime value must have a constant whole-net driver we can seed;
      // otherwise evaluating `drv` USUALLY fails and appends a diagnostic to
      // EvalContext...
      if (dep != &sym && !seed_dep(*dep, ctx, visiting, memo, depth + 1)) {
        deps_ready = false;
        break;
      }
    }
    // ...USUALLY, not always: see Seed_memo::spec_evals_left.
    if (!deps_ready && memo.spec_evals_left > 0) {
      --memo.spec_evals_left;
      deps_ready = true;
    }
    if (deps_ready) {
      auto cv = drv->eval(ctx);
      if (!cv.bad()) {
        ctx.createLocal(&sym, std::move(cv));
        ok = true;
      }
    }
  }
  visiting.erase(&sym);
  if (!ok && memo.depth_capped == capped_before) {
    memo.failed.insert(&sym);
  }
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
  slang::ast::EvalContext                             ctx(body_->asSymbol(), slang::ast::EvalFlags::IsScript);
  absl::flat_hash_set<const slang::ast::ValueSymbol*> visiting;
  Seed_memo                                           memo;
  Named_value_collector                               col;
  expr.visit(col);
  for (const auto* dep : col.syms) {
    // Best-effort, deliberately: `expr` itself may never READ the leaf that
    // could not be seeded (`{$bits(runtime_net){1'b0}}`, a conditional with a
    // constant condition). Let the single eval below decide -- that is one
    // eval per fold, so the memo still keeps this attempt linear.
    (void)seed_dep(*dep, ctx, visiting, memo, 0);
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

void Slang_context::emit_module_io(const slang::ast::InstanceSymbol& symbol, const Lnast_nid& in_tup, const Lnast_nid& out_tup) {
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
        emit_unsupported(port.location,
                         "unsupported-inout-port",
                         std::string("port '") + std::string(port.name) + "' is inout/ref, which --reader slang cannot lower");
        continue;
      }

      const auto& type   = port.getType();
      const bool  is_out = port.direction == slang::ast::ArgumentDirection::Out;

      // Unpacked-array port `T arr[N-1:0]` -> flat packed [N*elem_bits-1:0] IO.
      // Yosys places the declaration's rightmost element in the packed bus LSB,
      // so element access must preserve the declared range direction (see
      // flat_port_read/write).
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
            auto ei             = tinfo(elem);
            flat_mi.lower       = arr.range.lower();
            flat_mi.upper       = arr.range.upper();
            flat_mi.descending  = arr.range.isDescending();
            flat_mi.elem_bits   = ei.bits;
            flat_mi.elem_signed = ei.is_signed;
            flat_mi.size        = arr.range.width();
            io_bits             = ei.bits * static_cast<int>(flat_mi.size);
            io_signed           = false;  // flattened bus is just bits
            is_flat_array       = true;
          }
        }
        if (!is_flat_array) {
          emit_unsupported(port.location,
                           "unsupported-port-type",
                           std::string("port '") + std::string(port.name) + "' has a non-integral type");
          continue;
        }
      } else {
        auto ti   = tinfo(type);
        io_bits   = ti.bits;
        io_signed = ti.is_signed;
      }
      // M7: a qualifying packed-struct port becomes a TUPLE-typed io entry
      // (bundle) — per-field leaf ports after the SSA flatten, not a flat bus.
      const bool                 is_bundle = !is_flat_array && bundle_port_qualifies(port, module_name_of(symbol));
      // Provenance: a `[P-1:0]` dim naming a package param mints an imported
      // scalar alias (`pub type P_T = uN` in the package unit) the pyrope
      // re-emission prints as the port's type. A bundle port skips the alias
      // (its type slot is the field tuple, not a scalar width).
      std::optional<std::string> dim_alias;
      if (!is_flat_array && !is_bundle) {
        dim_alias = port_dim_alias(port, io_bits, io_signed);
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
          // A bundle output stays OUT of output_info_ — the whole-port X-poison
          // loop keys on it, and a bundle port gets per-FIELD poison instead.
          if (!is_bundle) {
            output_info_.emplace(internal, std::pair<int, bool>{io_bits, io_signed});
          }
        } else {
          input_syms_.insert(internal);
        }
        if (is_flat_array) {
          flat_port_syms_.insert(internal);
          mem_info_.emplace(internal, flat_mi);
        }
        if (is_bundle) {
          Struct_info si;
          // Body accesses use the "hand-flattened twin" LEAF form (2-child
          // store/read on the dotted leaf name `port.field`) — the exact form
          // upass.ssa's port flatten rewrites tuple ops INTO. Emitting it
          // directly rides the normal (battle-tested) scalar SSA: the tuple-op
          // route versions top-level field stores but leaves if-arm stores on
          // the base name and binds reads to the FIRST version, which breaks
          // the Verilog always_comb idiom (poison + default + conditional
          // overrides + field RMW).
          si.is_tuple = false;
          si.fields   = struct_port_fields(type);
          bundle_port_info_.emplace(internal, std::move(si));
        }
      }

      if (!is_flat_array && type.hasFixedRange() && !type.getFixedRange().isDescending()) {
        emit_warning(
            slang::SourceRange(port.location, port.location),
            "big-endian-port",
            "io",
            std::string("port '") + std::string(port.name) + "' is big-endian; flipping IO (mind mix/match with other modules)");
      }

      auto& ln    = *builder_.lnast;
      auto  entry = ln.add_child(is_out ? out_tup : in_tup, Lnast_ntype::create_store());
      ln.set_pending_srcid(mint_loc(port.location));
      ln.add_child(entry, Lnast_node::create_ref(var_name));
      ln.add_child(entry, Lnast_node::create_const("nil"));  // no default value
      if (is_bundle) {
        // Tuple type slot: tuple_add of per-field store(ref field, nil,
        // prim_type_int) — node-for-node the prp2lnast emit_arg_type shape for
        // `p:(q:u8,r:u8)`, which upass.ssa flatten_assign turns into dotted
        // leaf io entries (`p.q`, `p.r`) in field order (first = MSB).
        auto tup = ln.add_child(entry, Lnast_ntype::create_tuple_add());
        for (const auto& f : struct_port_fields(port.getType())) {
          auto fentry = ln.add_child(tup, Lnast_ntype::create_store());
          ln.add_child(fentry, Lnast_node::create_ref(f.name));
          ln.add_child(fentry, Lnast_node::create_const("nil"));
          emit_prim_type_int(fentry, f.bits, f.is_signed);
        }
      } else {
        emit_prim_type_int(entry, io_bits, io_signed);
      }
      if (dim_alias) {
        ln.add_io_type_name(var_name, *dim_alias);
      }
      if (is_out) {
        // `@[]` landing-cycle opt-out: the form foreign Verilog modules, which
        // carry no timing markings, ingest as.
        auto st = ln.add_child(entry, Lnast_ntype::create_stages());
        ln.add_child(st, Lnast_node::create_const("nil"));
        ln.add_child(st, Lnast_node::create_const("nil"));
      }
      ln.set_pending_srcid(hhds::SourceId_invalid);
    } else if (p->kind == SymbolKind::InterfacePort) {
      emit_unsupported(p->location,
                       "unsupported-interface-port",
                       std::string("interface port '") + std::string(p->name) + "' is not supported by --reader slang",
                       "use --reader yosys-slang for interface ports");
    } else if (p->kind == SymbolKind::MultiPort) {
      emit_unsupported(p->location,
                       "unsupported-multi-port",
                       std::string("multi-port '") + std::string(p->name) + "' is not supported by --reader slang yet");
    } else {
      emit_unsupported(p->location,
                       "unsupported-port-kind",
                       std::string("port '") + std::string(p->name) + "' has an unsupported kind");
    }
  }
}

void Slang_context::emit_local_param_consts(const slang::ast::Scope& body) {
  if (!options_.preserve_param_provenance) {
    return;
  }
  for (const auto& member : body.members()) {
    if (member.kind != slang::ast::SymbolKind::Parameter) {
      continue;
    }
    const auto& ps = member.as<slang::ast::ParameterSymbol>();
    if (owning_package(ps) != nullptr) {
      continue;  // package params ride `pkg.NAME`
    }
    const auto& cv = ps.getValue();
    if (!cv.isInteger()) {
      continue;
    }
    std::string name(ps.name);
    const bool  plain = !name.empty() && !std::isdigit(static_cast<unsigned char>(name.front()))
                        && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) != 0 || c == '_'; });
    if (!plain || used_names_.count(name) != 0u) {
      continue;  // colliding / exotic name — keep folding this param
    }
    // A single store, no declare: the prp_writer's single-store path renders
    // it in place as `const NAME = <rhs>`. The initializer lowers through the
    // NORMAL machinery, so a pkg-referencing defining expression stays
    // symbolic (`const THRESH = lpkg.BASE * 2`) and anything else folds; an
    // unread param is dropped by the writer's dead-signal elimination.
    std::string value = const_text(cv.integer());
    if (const auto* init = ps.getInitializer(); init != nullptr) {
      value = lower_rvalue(*init);
    }
    builder_.create_assign_stmts(name, value);
    local_param_lname_.emplace(&member, name);
    used_names_.insert(name);
  }
}

std::optional<std::string> Slang_context::port_dim_alias(const slang::ast::PortSymbol& port, int bits, bool is_signed) {
  if (!options_.preserve_param_provenance || is_signed || bits <= 1 || port.internalSymbol == nullptr) {
    return std::nullopt;  // signed dims would need an sN alias face — rare, deferred
  }
  const auto* vs = port.internalSymbol->as_if<slang::ast::ValueSymbol>();
  if (vs == nullptr) {
    return std::nullopt;
  }
  const auto* tsx = vs->getDeclaredType() != nullptr ? vs->getDeclaredType()->getTypeSyntax() : nullptr;
  if (tsx == nullptr) {
    return std::nullopt;
  }
  // `logic [P-1:0] x` is an IntegerTypeSyntax; a bare `input [P-1:0] x` is an
  // ImplicitTypeSyntax — both carry the packed dimensions.
  const slang::syntax::SyntaxNode* dim_node = nullptr;
  if (const auto* its = tsx->as_if<slang::syntax::IntegerTypeSyntax>(); its != nullptr && its->dimensions.size() == 1) {
    dim_node = its->dimensions[0];
  } else if (const auto* imp = tsx->as_if<slang::syntax::ImplicitTypeSyntax>(); imp != nullptr && imp->dimensions.size() == 1) {
    dim_node = imp->dimensions[0];
  }
  if (dim_node == nullptr) {
    return std::nullopt;
  }
  std::string dim;
  for (const char c : dim_node->toString()) {  // source text, e.g. "[VPU_FCMD_SZ-1:0]"
    if (!std::isspace(static_cast<unsigned char>(c))) {
      dim += c;
    }
  }
  // `[<ident>-1:0]` — anything else (expressions, non-zero lsb) keeps the uN
  if (dim.size() < 7 || dim.front() != '[' || !dim.ends_with("-1:0]")) {
    return std::nullopt;
  }
  const std::string ident = dim.substr(1, dim.size() - 6);
  if (ident.empty() || std::isdigit(static_cast<unsigned char>(ident.front()))
      || !std::all_of(ident.begin(), ident.end(), [](unsigned char c) { return std::isalnum(c) != 0 || c == '_'; })) {
    return std::nullopt;
  }
  const auto* scope = port.internalSymbol->getParentScope();
  if (scope == nullptr) {
    return std::nullopt;
  }
  const auto* psym = slang::ast::Lookup::unqualified(*scope, ident);
  if (psym == nullptr || psym->kind != slang::ast::SymbolKind::Parameter) {
    return std::nullopt;
  }
  const auto* pkg = owning_package(*psym);
  const auto* cv  = package_const_value(*psym);
  if (pkg == nullptr || cv == nullptr || !cv->isInteger()) {
    return std::nullopt;
  }
  auto v = cv->integer().as<int64_t>();
  if (!v || *v != bits) {
    return std::nullopt;  // the dim doesn't (or no longer) equal the port width — fold
  }
  std::string pkg_name(pkg->name);
  std::string alias                       = ident + "_T";
  // .first is the alias's MAX (its min is 0 — these are always `uN` port dims),
  // .second its `uN` print text. The max is emitted as the declare's
  // prim_type_int bound, which is where every consumer reads the range from; it
  // is never packed into a single "MAX|MIN" string.
  referenced_pkg_types_[pkg_name][alias]  = {mask_text(bits), absl::StrCat("u", bits)};
  // the driving param itself also exports (`pub comptime const SEL_W = 4`
  // next to `pub type SEL_W_T = u4`) — width provenance reads best in pairs
  referenced_pkg_params_[pkg_name][ident] = const_text(cv->integer());
  referenced_pkg_syms_[pkg_name]          = pkg;
  builder_.lnast->add_imported_package(pkg_name);
  return absl::StrCat(pkg_name, ".", alias);
}

// Pass 1 of the module conversion: classify processes and decide which
// variables are clocked state (reg_syms_). A variable is state when an
// edge-sensitive process writes it nonblocking. Blocking-written variables in
// edge processes stay process-local temps; one written there but read
// elsewhere has flop semantics this reader does not model yet -> diagnosed.
// Recurses into generate blocks: an `always_ff` inside a generate-for writing
// a module-scope array (`buffer_pc[buffer] <= f0_pc`) must classify that array
// as a reg BEFORE the declares run, or a comb read declares it `mut` first.
void Slang_context::collect_state_vars(const slang::ast::Scope& body) {
  for (const auto& member : body.members()) {
    if (member.kind == SymbolKind::ContinuousAssign) {
      // Note the symbol so the async-reset slice accumulator can tell a wholly
      // registered vector from one that is only partly register. A CONCATENATED
      // lvalue (`assign {a, q[0]} = …`) drives each lane, and lhs_base_symbol
      // answers nullptr for the concat itself, so walk the lanes.
      const auto& asg = member.as<slang::ast::ContinuousAssignSymbol>().getAssignment();
      if (asg.kind == slang::ast::ExpressionKind::Assignment) {
        const std::function<void(const slang::ast::Expression&)> note_cont_lhs = [&](const slang::ast::Expression& e) {
          if (e.kind == slang::ast::ExpressionKind::Concatenation) {
            for (const auto* op : e.as<slang::ast::ConcatenationExpression>().operands()) {
              note_cont_lhs(*op);
            }
            return;
          }
          if (const auto* lsym = lhs_base_symbol(e)) {
            cont_assign_syms_.insert(lsym);
          }
        };
        note_cont_lhs(asg.as<slang::ast::AssignmentExpression>().left());
      }
    }
    if (member.kind == SymbolKind::GenerateBlock) {
      const auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
      if (!gen.isUninstantiated) {
        collect_state_vars(gen);
      }
      continue;
    }
    if (member.kind == SymbolKind::GenerateBlockArray) {
      for (const auto* entry : member.as<slang::ast::GenerateBlockArraySymbol>().entries) {
        collect_state_vars(*entry);
      }
      continue;
    }
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
            auto edge  = tc.as<slang::ast::SignalEventControl>().edge;
            is_edge   |= edge == slang::ast::EdgeKind::PosEdge || edge == slang::ast::EdgeKind::NegEdge;
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
    // INFERRED LATCH from a BLOCKING write (2f-latch). A level-sensitive process
    // that leaves a variable unwritten on some path is a latch, not comb — the
    // textbook `always @(*) if (en) q = d;`. Before this, such a block was
    // classified as pure combinational logic and emitted `q = en ? d : X`, so the
    // hold path vanished: inou/prp/tests/equiv/latch_enable_hold.v round-tripped
    // into a stateless `comb`, and the reverse LEC only passed because the shared
    // encoder happened to refuse the reference side's Latch cell.
    //
    // Restricted to a level-sensitive `always` / `always_latch`. `always_comb` is
    // deliberately excluded: an incomplete assignment there is a LANGUAGE error
    // (IEEE 1800 requires it to be combinational), so quietly inserting state
    // would hide a source bug rather than model hardware.
    if (is_latch_block && !wc.blocking.empty()) {
      absl::flat_hash_set<const slang::ast::ValueSymbol*> definite;
      definite_blocking_writes(pbs.getBody(), definite);
      for (const auto* sym : wc.blocking) {
        if (definite.contains(sym)) {
          continue;  // assigned on every path: ordinary combinational logic
        }
        reg_syms_.insert(sym);
        latch_syms_.insert(sym);
      }
    }
  }
}

// Implements the documented half of collect_state_vars' contract that used to be
// checked only for module OUTPUTS: "one written [blocking, in an edge process]
// but read elsewhere has flop semantics this reader does not model yet ->
// diagnosed". A blocking-written var of an edge process is a legitimate
// process-local TEMP only while nothing outside that process reads it; the
// moment another process, a continuous assign or an instance port reads it, it
// is persistent state that survives the clock edge. Lowering it as a stateless
// `mut` silently DELETED the register -- `always @(posedge p) ms = ms + 1;` with
// `assign tick = ms` came out as a pure-combinational module whose output folded
// to the constant 1, and lgcheck refuted it.
//
// Reads are attributed to their enclosing procedural block; anything outside a
// block (continuous assigns, instance port connections) counts as an outside
// read on its own. A var blocking-written by two different edge processes is
// state as well -- neither can own it as a temp.
void Slang_context::collect_blocking_ff_state(const slang::ast::Scope& body) {
  using PB = slang::ast::ProceduralBlockSymbol;
  absl::flat_hash_map<const slang::ast::Symbol*, const PB*>                      owner;  // blocking-written -> its edge block
  absl::flat_hash_set<const slang::ast::Symbol*>                                 multi;  // ...written by more than one
  // Per-block reads (null block = module level). Kept as the collector's raw
  // vector, NOT a hash set: the resolve loop below iterates these and probes
  // `owner`, so a set here would buy nothing and cost one table per member --
  // and a Chisel-generated module has tens of thousands of members.
  std::vector<std::pair<const PB*, std::vector<const slang::ast::ValueSymbol*>>> reads;

  std::function<void(const slang::ast::Scope&)> walk = [&](const slang::ast::Scope& sc) {
    for (const auto& member : sc.members()) {
      if (member.kind == SymbolKind::GenerateBlock) {
        const auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
        if (!gen.isUninstantiated) {
          walk(gen);
        }
        continue;
      }
      if (member.kind == SymbolKind::GenerateBlockArray) {
        for (const auto* entry : member.as<slang::ast::GenerateBlockArraySymbol>().entries) {
          walk(*entry);
        }
        continue;
      }
      if (member.kind == SymbolKind::ProceduralBlock) {
        const auto&           pbs = member.as<PB>();
        Named_value_collector nv;
        pbs.getBody().visit(nv);
        reads.emplace_back(&pbs, std::move(nv.syms));
        Ff_blocking_collector wc;
        pbs.getBody().visit(wc);
        bool is_edge = false;
        if (pbs.procedureKind == slang::ast::ProceduralBlockKind::Always
            || pbs.procedureKind == slang::ast::ProceduralBlockKind::AlwaysFF) {
          const auto& stmt = pbs.getBody();
          if (stmt.kind == StatementKind::Timed) {
            const auto& timing = stmt.as<slang::ast::TimedStatement>().timing;
            auto        scan   = [&](const slang::ast::TimingControl& tc) {
              if (tc.kind == slang::ast::TimingControlKind::SignalEvent) {
                auto edge  = tc.as<slang::ast::SignalEventControl>().edge;
                is_edge   |= edge == slang::ast::EdgeKind::PosEdge || edge == slang::ast::EdgeKind::NegEdge;
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
        if (is_edge) {
          for (const auto* sym : wc.blocking) {
            if (auto [it, ins] = owner.try_emplace(sym, &pbs); !ins && it->second != &pbs) {
              multi.insert(sym);
            }
          }
        }
        continue;
      }
      // Everything else that can READ a signal at module level: continuous
      // assigns and instance port connections.
      if (const auto* mscope = member.as_if<slang::ast::Scope>(); mscope != nullptr && member.kind != SymbolKind::Instance) {
        walk(*mscope);
      }
      Named_value_collector nv;
      member.visit(nv);
      if (!nv.syms.empty()) {
        reads.emplace_back(nullptr, std::move(nv.syms));
      }
    }
  };
  walk(body);

  // Resolve owner-vs-reader by walking the READS once and probing `owner`, not
  // by walking `owner` and scanning every read set. Both compute the same set --
  // a blocking-written sym is state iff two edge blocks write it, or something
  // other than its owning block reads it -- but the owner-outer form is
  // |owner| x |reads| hash probes, and a symbol that is NOT read outside (the
  // common case, a genuine process-local temp) scans the whole `reads` vector
  // before concluding so. On XiangShan's Backend (1089 modules; Rob alone has
  // ~83k module-level members) that pair was 83% of every sample taken during
  // the slang->LNAST phase, and 28s of a 2m51 `lhd compile`. This form is
  // linear in the number of read refs; the same run is 2m23.
  // `multi` is a subset of `owner` (only a second, different writer puts a sym
  // there), so seeding from it is exact.
  for (const auto* sym : multi) {
    blocking_ff_state_.insert(sym);
  }
  for (const auto& [rb, rsyms] : reads) {
    for (const auto* sym : rsyms) {
      if (auto it = owner.find(sym); it != owner.end() && it->second != rb) {
        blocking_ff_state_.insert(sym);
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
    emit_unsupported(symbol.location,
                     "unsupported-instance-kind",
                     std::string("'") + std::string(symbol.name) + "' is not a module (interfaces/programs unsupported)");
    return false;
  }

  lowered_.emplace(body, nullptr);  // breaks recursion; reinserted at the end

  auto unit_name = module_name_of(symbol);

  // Save in-flight per-module state (a submodule definition lowers
  // recursively from its instantiation site).
  auto saved_builder           = std::move(builder_);
  auto saved_body              = body_;
  auto saved_eval              = std::move(eval_ctx_);
  auto saved_sym_lname         = std::move(sym_lname_);
  auto saved_local_params      = std::move(local_param_lname_);
  auto saved_used              = std::move(used_names_);
  auto saved_inputs            = std::move(input_syms_);
  auto saved_outputs           = std::move(output_syms_);
  auto saved_output_info       = std::move(output_info_);
  auto saved_bundle_ports      = std::move(bundle_port_info_);
  auto saved_bundle_shadow     = std::move(bundle_out_shadow_);
  auto saved_regs              = std::move(reg_syms_);
  auto saved_wires             = std::move(wire_syms_);
  auto saved_wire_split        = std::move(wire_split_tmp_);
  auto saved_wire_flat         = std::move(wire_split_flat_);
  auto saved_latches           = std::move(latch_syms_);
  auto saved_mems              = std::move(mem_syms_);
  auto saved_declared          = std::move(declared_);
  auto saved_prefix            = std::move(genblk_prefix_);
  auto saved_failed            = module_failed_;
  auto saved_proc_kind         = proc_kind_;
  auto saved_style             = std::move(proc_assign_style_);
  auto saved_blocking          = std::move(proc_blocking_written_);
  auto saved_bools             = std::move(bool_values_);
  auto saved_mem_info          = std::move(mem_info_);
  auto saved_reg_declared      = std::move(reg_declared_);
  auto saved_tuple_names       = std::move(tuple_type_names_);
  auto saved_emitted_types     = std::move(emitted_tuple_types_);
  auto saved_local_cnt         = local_cnt_;
  // The struct-classification collector sets are rebuilt per module below —
  // save them too, or the parent module resumes with the CHILD's sets after a
  // mid-emission recursive instance lowering, flipping is_scalar_struct_var
  // between a var's store and its later reads (Alu_3's `io_in = '{...}` stored
  // flat, but every read after the aluModule instance resolved through
  // never-written leaves -> io_out_valid stuck 0).
  auto saved_pattern_assigned  = std::move(struct_pattern_assigned_);
  auto saved_field_read        = std::move(struct_field_read_);
  auto saved_deep_accessed     = std::move(struct_deep_accessed_);
  auto saved_whole_copied      = std::move(struct_whole_copied_);
  auto saved_deep_written      = std::move(struct_deep_written_);
  auto saved_packed_mem_regs   = std::move(packed_mem_regs_);
  auto saved_array_reset_lanes = std::move(array_reset_lanes_);
  auto saved_pending_resets    = std::move(pending_async_resets_);

  builder_ = Lnast_builder();
  sym_lname_.clear();
  used_names_.clear();
  input_syms_.clear();
  output_syms_.clear();
  output_info_.clear();
  bundle_port_info_.clear();
  bundle_out_shadow_.clear();
  reg_syms_.clear();
  cont_assign_syms_.clear();
  wire_syms_.clear();
  wire_split_tmp_.clear();
  wire_split_flat_.clear();
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
  array_reset_lanes_.clear();
  pending_async_resets_.clear();
  local_cnt_ = 0;

  body_ = body;
  eval_ctx_.emplace(body->asSymbol(), slang::ast::EvalFlags::CacheResults);

  builder_.lnast = std::make_shared<Lnast>(unit_name);
  builder_.lnast->set_lambda_kind("mod");
  builder_.lnast->set_verilog_origin(true);  // Verilog flops are state, not pyrope feedforward stages
  auto root_nid = builder_.lnast->set_root(Lnast_ntype::create_top());
  builder_.lnast->set_srcid(root_nid, mint_loc(symbol.location));
  auto io_nid        = builder_.lnast->add_child(root_nid, Lnast_ntype::create_io());
  auto in_tup        = builder_.lnast->add_child(io_nid, Lnast_ntype::create_tuple_add());
  auto out_tup       = builder_.lnast->add_child(io_nid, Lnast_ntype::create_tuple_add());
  builder_.idx_stmts = builder_.lnast->add_child(root_nid, Lnast_ntype::create_stmts());

  // Port-vector SROA is body-profitable and internal-only. The port symbols
  // belong to the canonical body, exactly like emit_module_io's registrations.
  // Record the decision under the specialized unit name before emitting IO so
  // the definition and all recursive instance sites agree.
  Array_index_collector array_indices;
  body->visit(array_indices);
  if (!top_defs_.contains(std::string(symbol.getDefinition().name))) {
    absl::flat_hash_set<const slang::ast::ValueSymbol*>              port_symbols;
    absl::flat_hash_map<const slang::ast::ValueSymbol*, std::string> port_names;
    for (const auto* p : body->getPortList()) {
      if (p->kind != slang::ast::SymbolKind::Port) {
        continue;
      }
      const auto& port = p->as<slang::ast::PortSymbol>();
      if (port.internalSymbol == nullptr) {
        continue;
      }
      if (const auto* value = port.internalSymbol->as_if<slang::ast::ValueSymbol>()) {
        port_symbols.insert(value);
        port_names.emplace(value, std::string(port.name));
      }
    }
    Packed_vector_port_collector pvc;
    pvc.ports     = &port_symbols;
    pvc.loop_vars = &array_indices.loop_vars;
    body->visit(pvc);
    for (const auto& [port, use] : pvc.info) {
      if (use.static_selects < 2 || use.dynamic_select) {
        continue;
      }
      if (const auto it = port_names.find(port); it != port_names.end()) {
        vector_bundle_ports_.insert(absl::StrCat(unit_name, "\x1f", it->second));
      }
    }
  }

  emit_module_io(symbol, in_tup, out_tup);
  local_param_lname_.clear();
  emit_local_param_consts(*body);
  collect_state_vars(*body);
  collect_blocking_ff_state(*body);
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
  // Packed arrays written through a whole-array RANGE select (`arr[2:1] <= v`).
  // Only the FLAT bus representation lowers that write correctly (set_mask
  // composes it), so these must stay out of the packed-2D memory classifier
  // below -- see Array_range_write_collector.
  absl::flat_hash_set<const slang::ast::ValueSymbol*> array_range_written;
  {
    absl::flat_hash_map<const slang::ast::ValueSymbol*, int> array_whole_assigned;
    absl::flat_hash_set<const slang::ast::ValueSymbol*>      array_part_written;
    Array_range_write_collector                              rwc;
    rwc.range_written  = &array_range_written;
    rwc.whole_assigned = &array_whole_assigned;
    rwc.part_written   = &array_part_written;
    body->visit(rwc);
    // A NET INITIALIZER (`wire [N-1:0][W-1:0] x = {lane, lane, …};` — how
    // firtool spells every combinational lane table) is bound by slang's
    // bindRValue and is NEVER wrapped in an AssignmentExpression, so the
    // visitor above cannot see it and the array looks undriven. lower_members
    // already models it as its own driver kind (SymbolKind::Net +
    // getInitializer(), separate from ContinuousAssign) — seed the same
    // candidacy here, or `wire x = {…}` and `assign x = {…}` classify
    // differently for no reason and the Type-C repair below silently never
    // reaches the initializer spelling.
    std::function<void(const slang::ast::Scope&)> seed_net_init_arrays = [&](const slang::ast::Scope& sc) {
      for (const auto& member : sc.members()) {
        if (member.kind == SymbolKind::GenerateBlock) {
          const auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
          if (!gen.isUninstantiated) {
            seed_net_init_arrays(gen);
          }
          continue;
        }
        if (member.kind == SymbolKind::GenerateBlockArray) {
          for (const auto* entry : member.as<slang::ast::GenerateBlockArraySymbol>().entries) {
            seed_net_init_arrays(*entry);
          }
          continue;
        }
        if (member.kind != SymbolKind::Net) {
          continue;  // a VARIABLE initializer is a time-0 value, not a driver
        }
        const auto& ns = member.as<slang::ast::ValueSymbol>();
        if (ns.getInitializer() != nullptr && Array_range_write_collector::is_candidate(ns)) {
          ++array_whole_assigned[&ns];
        }
      }
    };
    seed_net_init_arrays(*body);
    // TYPE-C SELF-REFERENCE. A packed-array net whose own whole-array driver
    // reads one of its OWN elements (`assign vec = '{vec[0] ^ a, b}`) is
    // acyclic at BIT level -- substituting the sibling lane's driver resolves
    // it -- but it is a false cycle at word level. Declared as an ordinary
    // `mut`, the sibling read binds to the net's POISON init instead of to the
    // sibling's driver, which is a silent miscompile (measured on
    // inou/prp/tests/equiv/array_selfref.v: `z = a_1 ^ ?` instead of
    // `z = a_0 ^ a_1`).
    //
    // Packed-array SROA used to fix this by splitting the array into
    // per-element WIRE leaves. It does not need to: declaring the net as a
    // Pyrope `wire` hands it to graph/split_selfref's
    // split_packed_selfref_wire, which upass/tolg calls at wire binding and
    // which dissolves exactly this shape. Same repair, owned by the pass that
    // already owns packed self-reference, and with no per-element leaves and
    // no provenance attributes to carry afterwards.
    for (const auto& [sym, whole_drivers] : array_whole_assigned) {
      if (reg_syms_.contains(sym) || input_syms_.contains(sym) || output_syms_.contains(sym)) {
        continue;  // ports are flat; a clocked array is state, not a net
      }
      if (whole_drivers != 1) {
        // More than one whole-array driver (two continuous assigns, or a
        // conditional procedural rewrite): `whole_net_driver` below reports only
        // the FIRST, so a `wire` promotion would bind the net to that one and
        // lose the rest. Keep it a `mut`.
        continue;
      }
      if (array_part_written.contains(sym)) {
        // A whole-array driver PLUS an element / partial write is more than one
        // driver, and a Pyrope `wire` is single-driver by contract: promoting
        // this net would trade the false word-level cycle for a dropped write.
        // The old SROA gate refused the same shape (`!in.elem_written`).
        continue;
      }
      const auto* drv = whole_net_driver(*sym);
      if (drv == nullptr) {
        continue;
      }
      absl::flat_hash_set<const slang::ast::ValueSymbol*> visiting;
      if (driver_reads_target(*drv, *sym, visiting, 0)) {
        wire_syms_.insert(sym);
      }
    }
  }
  // Harvest `initial begin mem[k]=v; end` power-on contents before the declares
  // emit (declare_unpacked folds them into the reg array's initializer). Walk
  // instantiated generate scopes too: cgen_memory_* guards its constant init
  // loop with `generate if (INIT_EN)`.
  std::function<void(const slang::ast::Scope&)> harvest_mem_inits = [&](const slang::ast::Scope& scope) {
    for (const auto& member : scope.members()) {
      if (member.kind == slang::ast::SymbolKind::GenerateBlock) {
        const auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
        if (!gen.isUninstantiated) {
          harvest_mem_inits(gen);
        }
        continue;
      }
      if (member.kind == slang::ast::SymbolKind::GenerateBlockArray) {
        for (const auto* entry : member.as<slang::ast::GenerateBlockArraySymbol>().entries) {
          harvest_mem_inits(*entry);
        }
        continue;
      }
      if (member.kind == slang::ast::SymbolKind::ProceduralBlock
          && member.as<slang::ast::ProceduralBlockSymbol>().procedureKind == slang::ast::ProceduralBlockKind::Initial) {
        collect_mem_inits(member.as<slang::ast::ProceduralBlockSymbol>().getBody());
      }
    }
  };
  harvest_mem_inits(*body);
  // Classify array selectors before declarations. Packed 2-D register handling
  // below uses runtime indexing to choose a Memory. For unpacked arrays, state
  // always remains a Memory; selector constness only controls whether a plain
  // combinational array can be predeclared as its packed accumulator.
  {
    auto is_runtime_sel = [&](const slang::ast::Expression* sel) {
      if (try_eval_int(*sel)) {
        return false;  // folds to a constant (genvar/param/const index)
      }
      // Not directly foldable: still constant after unroll if it references only
      // loop-induction vars / params / genvars. A runtime signal makes it dynamic.
      Static_selector_scan ss;
      ss.loop_vars = &array_indices.loop_vars;
      sel->visit(ss);
      return !ss.all_static;
    };
    for (const auto& [sym, sel] : array_indices.selects) {
      if (is_runtime_sel(sel)) {
        runtime_indexed_arrays_.insert(sym);
      }
    }
    // A PACKED 2-D reg `[N][W]` (W>1) is an ARRAY: declare it as `reg x:[N]uW`
    // (one __memory node) rather than flattening it into one N*W-bit flop that
    // every element access then has to bit-slice back out.
    //
    // The selector being constant or not makes NO difference here: both spell
    // the same LGraph memory, and turning a constant-addressed one back into
    // per-element flops is a SYNTHESIS optimization, not a front-end choice.
    // (This gate used to demand `is_runtime_sel(sel)` "for yosys-slang
    // compatibility" — that kept `reg [15:0][56:0] data` as `reg data:u912`
    // with 16 hand-computed `data#[57k..=57k+56]` slices, which is exactly the
    // array-ness the Pyrope emission was losing.)
    // …with ONE exception: an array whose reset (or any other whole-array
    // constant load) is a PER-ELEMENT pattern. That value reaches the declare
    // as one scalar `initial` attr, and a scalar initial on `reg x:[N]uW`
    // BROADCASTS — so `deqPtrVec <= '{…7,6,5,4,3,2,1,0}` came out as every
    // entry resetting to `'b0` and LEC-REFUTED NewRobDeqPtrWrapper. Until the
    // reset can be spelled per entry, those stay a flat bus.
    absl::flat_hash_set<const slang::ast::ValueSymbol*> array_pattern_loaded;
    {
      // ONLY the async-reset arms. Those are the sole place a whole-array
      // CONSTANT load turns into the declare's scalar `initial`, and
      // try_eval_const_net (below) seeds and folds every net driver reachable
      // from the expression — running it over the whole body walks the entire
      // datapath instead. A block with a single edge trigger has no async rung
      // and therefore no reset value to lose.
      std::vector<const slang::ast::Statement*>     reset_arms;
      std::function<void(const slang::ast::Scope&)> harvest_reset_arms = [&](const slang::ast::Scope& scope) {
        for (const auto& member : scope.members()) {
          if (member.kind == SymbolKind::GenerateBlock) {
            const auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
            if (!gen.isUninstantiated) {
              harvest_reset_arms(gen);
            }
            continue;
          }
          if (member.kind == SymbolKind::GenerateBlockArray) {
            for (const auto* entry : member.as<slang::ast::GenerateBlockArraySymbol>().entries) {
              harvest_reset_arms(*entry);
            }
            continue;
          }
          if (member.kind != SymbolKind::ProceduralBlock) {
            continue;
          }
          const auto& pbs = member.as<slang::ast::ProceduralBlockSymbol>();
          if (pbs.procedureKind != slang::ast::ProceduralBlockKind::Always
              && pbs.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF) {
            continue;
          }
          const auto& stmt = pbs.getBody();
          if (stmt.kind != StatementKind::Timed) {
            continue;
          }
          const auto& timed  = stmt.as<slang::ast::TimedStatement>();
          int         nedges = 0;
          auto        count  = [&](const slang::ast::TimingControl& tc) {
            if (tc.kind == slang::ast::TimingControlKind::SignalEvent) {
              const auto edge = tc.as<slang::ast::SignalEventControl>().edge;
              if (edge == slang::ast::EdgeKind::PosEdge || edge == slang::ast::EdgeKind::NegEdge) {
                ++nedges;
              }
            }
          };
          if (timed.timing.kind == slang::ast::TimingControlKind::EventList) {
            for (const auto* ev : timed.timing.as<slang::ast::EventListControl>().events) {
              count(*ev);
            }
          } else {
            count(timed.timing);
          }
          // Peel the same if/else rungs the reset extraction peels: one per
          // extra edge trigger, each rung's THEN arm holding its reset values.
          const slang::ast::Statement* b = &timed.stmt;
          for (int rung = nedges - 1; rung > 0; --rung) {
            while (b->kind == StatementKind::Block) {
              b = &b->as<slang::ast::BlockStatement>().body;
            }
            if (b->kind == StatementKind::List) {
              const slang::ast::Statement* only_cond = nullptr;
              for (const auto* sub : b->as<slang::ast::StatementList>().list) {
                if (sub->kind == StatementKind::Conditional) {
                  only_cond = sub;
                  break;
                }
              }
              if (only_cond == nullptr) {
                break;
              }
              b = only_cond;
            }
            if (b->kind != StatementKind::Conditional) {
              break;
            }
            const auto& cs = b->as<slang::ast::ConditionalStatement>();
            reset_arms.push_back(&cs.ifTrue);
            if (cs.ifFalse == nullptr) {
              break;
            }
            b = cs.ifFalse;
          }
        }
      };
      harvest_reset_arms(*body);

      Whole_store_collector wsc;
      for (const auto* arm : reset_arms) {
        arm->visit(wsc);
      }
      // A memory carries its reset only when the DATAPATH also drives the whole
      // array (`spec_table <= spec_table_next`): that whole-array write is what
      // becomes the memory's `update` bus, and only the update-bus lowering has
      // a reset/init bus to hang the reset on. With per-entry writes alone the
      // reset is silently DROPPED — measured: PMAEntryHandleModule,
      // RegCacheAgeTimer_1 and RobEnqPtrWrapper all LEC-REFUTED that way, and a
      // 4-line `reg m:[4]u8:[init=0, reset_pin=ref rst, async=true]` loses its
      // reset today with no diagnostic at all. Such arrays stay a flat flop bus,
      // which does reset correctly, until the memory lowering grows a reset for
      // the per-port shape.
      absl::flat_hash_set<const slang::ast::ValueSymbol*> datapath_whole_written;
      if (!wsc.touched.empty()) {  // no reset arm wrote anything: the whole-body walk has no consumer
        absl::flat_hash_set<const slang::ast::Expression*> reset_rhs;
        for (const auto& [sym, rhs] : wsc.stores) {
          reset_rhs.insert(rhs);
        }
        Whole_store_collector all;
        body->visit(all);
        for (const auto& [sym, rhs] : all.stores) {
          if (!reset_rhs.contains(rhs)) {
            datapath_whole_written.insert(sym);
          }
        }
      }
      // EVERY array a reset arm writes — whole or per entry — needs the update
      // bus, not just the ones with a splittable constant pattern: a uniform
      // `if (rst) arr <= '0` loses its reset the same way.
      for (const auto* sym : wsc.touched) {
        int64_t n = 0, lo = 0;
        int     w  = 0;
        bool    sg = false;
        if (is_packed_2d_array(sym->getType(), n, w, sg, lo) && !datapath_whole_written.contains(sym)) {
          array_pattern_loaded.insert(sym);
        }
      }
      for (const auto& [sym, rhs] : wsc.stores) {
        int64_t n = 0, lo = 0;
        int     w  = 0;
        bool    sg = false;
        if (!is_packed_2d_array(sym->getType(), n, w, sg, lo) || n <= 1 || w <= 0) {
          continue;
        }
        if (array_pattern_loaded.contains(sym)) {
          continue;  // staying flat; no lanes needed
        }
        auto cv = try_eval_const_net(*rhs);
        if (!cv || !cv->isInteger()) {
          continue;  // a runtime whole-array load carries no `initial` to broadcast
        }
        auto packed = Dlop::from_pyrope(const_text(cv->integer()));
        if (!packed || packed->is_invalid()) {
          array_pattern_loaded.insert(sym);  // cannot split it -> stay a flat bus
          continue;
        }
        // Lane k is memory ADDRESS k, i.e. declared index `lo + k`
        // (build_unpacked_index addresses an element as `index - lower` in both
        // range directions). Its home in the packed word is `k*w` only when the
        // outer range DESCENDS; an ascending `[0:N-1]` puts index `lo + k` at
        // `(upper - index)*w = (n-1-k)*w`, so the pattern would come out
        // reversed. Every other index computation in this reader branches on
        // isDescending() the same way.
        const auto& outer      = sym->getType().getCanonicalType().as<slang::ast::PackedArrayType>();
        const bool  descending = outer.range.isDescending();

        std::vector<std::string> lanes;
        lanes.reserve(static_cast<size_t>(n));
        for (int64_t k = 0; k < n; ++k) {
          const int64_t pos = descending ? k : (n - 1 - k);
          const int     lsb = static_cast<int>(pos * w);
          lanes.emplace_back(packed->get_mask_op(*Dlop::get_mask_value(lsb + w - 1, lsb))->to_pyrope());
        }
        if (std::all_of(lanes.begin(), lanes.end(), [&](const std::string& l) { return l == lanes.front(); })) {
          continue;  // uniform: the scalar `initial` attr broadcasts correctly
        }
        // A pattern. Keep it per entry so the array survives as an array; the
        // async-reset lowering skips its scalar `initial` for these symbols.
        array_reset_lanes_[sym] = std::move(lanes);
      }
    }
    // One decision per SYMBOL: the selector no longer participates (a constant
    // and a runtime index spell the same LGraph memory), so a 16-entry bank
    // must not re-run the whole gate once per element access.
    absl::flat_hash_set<const slang::ast::ValueSymbol*> packed_sel_seen;
    for (const auto& entry : array_indices.packed_selects) {
      const auto* sym = entry.first;
      if (!packed_sel_seen.insert(sym).second) {
        continue;
      }
      int64_t n = 0, lo = 0;
      int     w  = 0;
      bool    sg = false;
      // An OUTPUT reg must stay a flat flop bus — its q pin IS the port driver;
      // memory-izing it would leave the output undriven (dcache's
      // `output [Sets][Ways] hlock_state_o` read at runtime indices). So must
      // an array written through a whole-array range select: a Memory has one
      // write port per element store and no place to put `arr[2:1] <= v`, so
      // the write is silently dropped (the memory instantiates with
      // `wr_enable_0(1'b0)`).
      if (reg_syms_.contains(sym) && !output_syms_.contains(sym) && !array_range_written.contains(sym)
          && !array_pattern_loaded.contains(sym) && is_packed_2d_array(sym->getType(), n, w, sg, lo)) {
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
    for (const auto* sym : emit_ordered(reg_syms_)) {
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
  // A MULTI-dimensional array is also pre-declared from INSIDE a generate block
  // (`top` false below), because its seed has to dominate every element store
  // wherever the array lives; the flatten-branch rules stay module-scope only,
  // where they were tuned.
  std::function<void(const slang::ast::Scope&, bool)> predeclare_arrays = [&](const slang::ast::Scope& scope, bool top) {
    for (const auto& member : scope.members()) {
      if (member.kind == slang::ast::SymbolKind::GenerateBlock) {
        const auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
        if (!gen.isUninstantiated) {
          predeclare_arrays(gen, /*top=*/false);
        }
        continue;
      }
      if (member.kind == slang::ast::SymbolKind::GenerateBlockArray) {
        for (const auto* entry : member.as<slang::ast::GenerateBlockArraySymbol>().entries) {
          predeclare_arrays(*entry, /*top=*/false);
        }
        continue;
      }
      if (member.kind != slang::ast::SymbolKind::Variable) {
        continue;
      }
      const auto& vsym = member.as<slang::ast::VariableSymbol>();
      if (reg_syms_.contains(&vsym) || declared_.contains(&vsym)) {
        continue;
      }
      const auto& ct = vsym.getType().getCanonicalType();
      // A module-scope STRUCT variable is pre-declared too — but INSIDE
      // lower_members, right after the wire classification (see the
      // "struct pre-declare" block there). Pre-declaring it here locked every
      // struct net into `mut`, because declare_struct_leaves consults
      // `wire_syms_`, which lower_members only fills later: a reader sorted
      // ahead of its writer then resolved to nil and the connection was SEVERED
      // (minion's id_vpu_core_ctrl, f8_trans_rom_response). It still lands at
      // module top, so the reason this pre-declare exists at all — a lazy
      // declare would emit the leaf declares INSIDE the first use's if/uif arm,
      // and a dotted poison store in a unique_if arm survives the branch merge
      // in a field-store form tolg cannot lower (trans_top's f5_rom_response_l)
      // — is unchanged.
      if (ct.kind != slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
        continue;
      }
      // Walk the whole unpacked dim chain: a MULTI-dimensional array's element
      // type is another unpacked array, and stopping at the first level skipped
      // every 2-D array here.
      const slang::ast::Type* leaf     = &ct.as<slang::ast::FixedSizeUnpackedArrayType>().elementType.getCanonicalType();
      bool                    multidim = false;
      while (leaf->kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
        multidim = true;
        leaf     = &leaf->as<slang::ast::FixedSizeUnpackedArrayType>().elementType.getCanonicalType();
      }
      const auto& elem = *leaf;
      if (!elem.isIntegral()) {
        continue;
      }
      const bool aggregate     = elem.isStruct() || elem.isPackedUnion();
      const bool const_indexed = !runtime_indexed_arrays_.contains(&vsym);
      // A MULTI-dimensional array is pre-declared whatever its selectors are: it
      // never takes the flatten branch, so declare_unpacked gives it a linearized
      // array plus the whole-array poison seed its first element store splices
      // onto — and a LAZY declare would drop that seed inside the first use's
      // if/case arm, leaving the array unseeded on every other path.
      if (multidim || (top && (aggregate || const_indexed))) {
        declare_value_symbol(vsym, /*force_reg=*/false);
      }
    }
  };
  predeclare_arrays(*body, /*top=*/true);

  // The X-default poison-init for combinational outputs is emitted inside
  // lower_members (after wire classification: a wire-classified output must
  // NOT get the poison store — the wire is single-driver, and a split wire's
  // accumulator carries the poison as its declare init instead).

  lower_members(*body);
  finalize_pending_async_resets();

  bool ok = !module_failed_;
  if (ok) {
    lowered_[body] = builder_.lnast;
    ordered_lnasts_.push_back(builder_.lnast);
  }

  // restore the enclosing module's state
  builder_                 = std::move(saved_builder);
  body_                    = saved_body;
  eval_ctx_                = std::move(saved_eval);
  sym_lname_               = std::move(saved_sym_lname);
  local_param_lname_       = std::move(saved_local_params);
  used_names_              = std::move(saved_used);
  input_syms_              = std::move(saved_inputs);
  output_syms_             = std::move(saved_outputs);
  output_info_             = std::move(saved_output_info);
  bundle_port_info_        = std::move(saved_bundle_ports);
  bundle_out_shadow_       = std::move(saved_bundle_shadow);
  reg_syms_                = std::move(saved_regs);
  wire_syms_               = std::move(saved_wires);
  wire_split_tmp_          = std::move(saved_wire_split);
  wire_split_flat_         = std::move(saved_wire_flat);
  latch_syms_              = std::move(saved_latches);
  mem_syms_                = std::move(saved_mems);
  declared_                = std::move(saved_declared);
  genblk_prefix_           = std::move(saved_prefix);
  module_failed_           = saved_failed;
  proc_kind_               = saved_proc_kind;
  proc_assign_style_       = std::move(saved_style);
  proc_blocking_written_   = std::move(saved_blocking);
  bool_values_             = std::move(saved_bools);
  mem_info_                = std::move(saved_mem_info);
  reg_declared_            = std::move(saved_reg_declared);
  tuple_type_names_        = std::move(saved_tuple_names);
  emitted_tuple_types_     = std::move(saved_emitted_types);
  local_cnt_               = saved_local_cnt;
  struct_pattern_assigned_ = std::move(saved_pattern_assigned);
  struct_field_read_       = std::move(saved_field_read);
  struct_deep_accessed_    = std::move(saved_deep_accessed);
  struct_whole_copied_     = std::move(saved_whole_copied);
  struct_deep_written_     = std::move(saved_deep_written);
  packed_mem_regs_         = std::move(saved_packed_mem_regs);
  array_reset_lanes_       = std::move(saved_array_reset_lanes);
  pending_async_resets_    = std::move(saved_pending_resets);

  return ok;
}

// comp_type_array declare for an unpacked array (a verilog memory). The
// fwd=0 attr rides ahead of the declare: verilog nonblocking memory writes
// never forward to same-cycle reads (Pyrope reg arrays default to
// program-order forwarding).
// Walk an `initial` block body, recording constant `mem[const] = const` element
// writes (the standard memory power-on idiom) into mem_init_vals_. Direct
// assignments and constant-bounded for loops are captured; anything else is
// silently skipped (the block stays "ignored").
void Slang_context::collect_mem_inits(const slang::ast::Statement& stmt) {
  using slang::ast::ExpressionKind;
  using slang::ast::StatementKind;
  switch (stmt.kind) {
    case StatementKind::List:
      for (const auto* s : stmt.as<slang::ast::StatementList>().list) {
        collect_mem_inits(*s);
      }
      return;
    case StatementKind::Block              : collect_mem_inits(stmt.as<slang::ast::BlockStatement>().body); return;
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
    case StatementKind::ForLoop: {
      if (!eval_ctx_) {
        return;
      }
      const auto                                  saved_inits = mem_init_vals_;
      const auto&                                 loop        = stmt.as<slang::ast::ForLoopStatement>();
      std::vector<const slang::ast::ValueSymbol*> locals;
      bool                                        valid = true;
      for (const auto* lv : loop.loopVars) {
        slang::ConstantValue init;
        if (const auto* ie = lv->getInitializer()) {
          if (auto cv = try_eval(*ie)) {
            init = *cv;
          }
        }
        if (init.bad()) {
          valid = false;
          break;
        }
        eval_ctx_->createLocal(lv, init);
        locals.push_back(lv);
      }
      if (valid && loop.loopVars.empty()) {
        for (const auto* ie : loop.initializers) {
          if (ie->kind != ExpressionKind::Assignment) {
            valid = false;
            break;
          }
          const auto& assign = ie->as<slang::ast::AssignmentExpression>();
          if (assign.left().kind != ExpressionKind::NamedValue) {
            valid = false;
            break;
          }
          const auto& sym = assign.left().as<slang::ast::NamedValueExpression>().symbol;
          auto        cv  = try_eval(assign.right());
          if (!cv) {
            valid = false;
            break;
          }
          eval_ctx_->createLocal(&sym, *cv);
          locals.push_back(&sym);
        }
      }

      int iterations = 0;
      while (valid) {
        if (iterations++ >= options_.unroll_limit) {
          valid = false;
          break;
        }
        if (loop.stopExpr != nullptr) {
          auto stop = try_eval(*loop.stopExpr);
          if (!stop) {
            valid = false;
            break;
          }
          if (!stop->isTrue()) {
            break;
          }
        }
        collect_mem_inits(loop.body);
        if (loop.stopExpr == nullptr && loop.steps.empty()) {
          valid = false;
          break;
        }
        for (const auto* step : loop.steps) {
          if (!try_eval(*step)) {
            valid = false;
            break;
          }
        }
      }
      for (const auto* lv : locals) {
        eval_ctx_->deleteLocal(lv);
      }
      if (!valid) {
        mem_init_vals_ = saved_inits;  // never retain a partial loop initialization
      }
      return;
    }
    default: return;
  }
}

bool Slang_context::declare_unpacked(const slang::ast::ValueSymbol& sym, bool is_reg) {
  const auto& ct = sym.getType().getCanonicalType();
  if (ct.kind != slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
    emit_unsupported(sym.location,
                     "unsupported-array-kind",
                     std::string("array '") + std::string(sym.name) + "' is not a fixed-size unpacked array");
    return false;
  }
  const auto&                arr = ct.as<slang::ast::FixedSizeUnpackedArrayType>();
  // Peel every unpacked dim (outermost first): a multi-dim `T m [A][B]`
  // linearizes to a 1-D memory of A*B elements of T (row-major, innermost dim
  // contiguous); access sites fold the full `m[i][j]` selector chain into one
  // linear index (build_unpacked_index).
  std::vector<Mem_info::Dim> dims;
  const slang::ast::Type*    ep = &ct;
  while (ep->getCanonicalType().kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
    const auto& a = ep->getCanonicalType().as<slang::ast::FixedSizeUnpackedArrayType>();
    dims.push_back({a.range.lower(), static_cast<int64_t>(a.range.width())});
    ep = &a.elementType;
  }
  const auto& elem = ep->getCanonicalType();
  if (!elem.isIntegral()) {
    emit_unsupported(sym.location,
                     "unsupported-mem-element",
                     std::string("memory '") + std::string(sym.name) + "' has a non-integral element type");
    return false;
  }
  int64_t total = 1;
  for (const auto& d : dims) {
    total *= d.width;
  }
  auto ei = tinfo(elem);

  // Flatten a combinational array to a packed bus. This preserves X in each
  // undriven Verilog-net element, aggregate sub-writes compose through set_mask,
  // and flat_port_read/write implement dynamic element access with shifts.
  const int64_t flat_bits = static_cast<int64_t>(ei.bits) * total;
  const bool    has_init  = [&] {
    auto it = mem_init_vals_.find(&sym);
    return it != mem_init_vals_.end() && !it->second.empty();
  }();
  // Flatten combinational arrays to one packed accumulator so partial writes
  // compose without inferring state. Stateful unpacked arrays stay native
  // memories regardless of whether their selectors happen to be constant:
  // selector constness is a synthesis optimization, while preserving the
  // aggregate identity is required by whole-array read_all/update semantics and
  // by Verilog -> Pyrope state correspondence. Multi-dimensional arrays always
  // take the linearized memory path.
  const bool flat_io_port = flat_port_syms_.contains(&sym);
  if (dims.size() == 1 && (flat_io_port || (!is_reg && !has_init)) && flat_bits > 0 && flat_bits <= 65536) {
    if (has_init) {
      // The flat branch has no INIT representation (the Memory branch below is
      // the only one that emits mem_init_vals_). A flat IO port reaches here
      // even WITH power-on contents, so say so instead of dropping them.
      emit_warning(slang::SourceRange(sym.location, sym.location),
                   "mem-init-ignored",
                   "unsupported",
                   std::string("power-on contents of flattened array port '") + std::string(sym.name) + "' are ignored");
    }
    Mem_info fmi;
    fmi.lower       = arr.range.lower();
    fmi.upper       = arr.range.upper();
    fmi.descending  = arr.range.isDescending();
    fmi.elem_bits   = ei.bits;
    fmi.elem_signed = ei.is_signed;
    fmi.size        = arr.range.width();
    mem_info_.insert_or_assign(&sym, fmi);
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
      // An unpacked combinational array is a bundle of Verilog nets. Bits with
      // no driver are X, not zero. Keep it as one packed accumulator (dynamic
      // element access is the flat_port shift/mask path) and poison every bit;
      // the element stores below replace only the bits they actually drive.
      // Declared width + the width-taking `0sb?` wildcard, not a flat_bits-long
      // run of `?` (these buses reach 1024 bits). Same value either way.
      builder_.create_declare_stmts(name,
                                    "mut",
                                    int_max_str(static_cast<int>(flat_bits), false),
                                    int_min_str(static_cast<int>(flat_bits), false),
                                    "0sb?");
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
    mi.upper       = arr.range.upper();
    mi.descending  = arr.range.isDescending();
    mi.elem_bits   = ei.bits;
    mi.elem_signed = false;
    mi.size        = total;
    mi.dims        = dims;
    mi.is_tuple    = true;
    mi.type_name   = tuple_type_name(elem);
    for (const auto& f : st.membersOfType<slang::ast::FieldSymbol>()) {
      auto fi = tinfo(f.getType());
      mi.fields.push_back({std::string(f.name), static_cast<int64_t>(f.bitOffset), fi.bits, fi.is_signed});
    }
    // insert_or_assign, NOT emplace: emit_module_io already registered a flat
    // Mem_info for every unpacked-array PORT, and emplace would silently keep
    // that record -- `rec` below would then drive emit_tuple_typedef from a
    // descriptor with no fields and is_tuple=false.
    mem_info_.insert_or_assign(&sym, mi);
    mem_syms_.insert(&sym);

    const auto& rec  = mem_info_.at(&sym);
    auto        name = lname_of(sym);
    auto&       ln   = *builder_.lnast;
    set_pending_loc(sym.location);
    emit_tuple_typedef(rec);
    // Verilog nonblocking memory reads see the committed contents, i.e. NO
    // read forwards any write AND the value is DEFINED: ordering="old" (an
    // all-zeros `fwd` matrix and an all-zeros `undef` matrix). NOT
    // ordering="none" — that now means the collision window is UNDEFINED (x),
    // which would make every imported memory a formal don't-care. The reader
    // cannot use the ordering="program" default either, because it emits
    // memory reads AFTER the writes in LNAST order, which program order
    // would (correctly, for Pyrope source) forward. detuple does
    // not split attr_set, so emit the attr on each post-split `mem.field` name
    // directly (it lands before the per-field declares detuple synthesizes).
    if (is_reg) {
      for (const auto& f : rec.fields) {
        auto aidx = builder_.add_child(Lnast_ntype::create_attr_set());
        ln.add_child(aidx, Lnast_node::create_ref(absl::StrCat(name, ".", f.name)));
        ln.add_child(aidx, Lnast_node::create_const("ordering"));
        ln.add_child(aidx, Lnast_node::create_const("\"old\""));
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
  mi.upper       = arr.range.upper();
  mi.descending  = arr.range.isDescending();
  mi.elem_bits   = ei.bits;
  mi.elem_signed = ei.is_signed;
  mi.size        = total;
  mi.dims        = dims;
  mem_info_.insert_or_assign(&sym, mi);  // see the note on the struct branch above
  mem_syms_.insert(&sym);

  auto  name = lname_of(sym);
  auto& ln   = *builder_.lnast;
  set_pending_loc(sym.location);
  if (is_reg) {
    auto aidx = builder_.add_child(Lnast_ntype::create_attr_set());
    ln.add_child(aidx, Lnast_node::create_ref(name));
    ln.add_child(aidx, Lnast_node::create_const("ordering"));
    ln.add_child(aidx, Lnast_node::create_const("\"old\""));
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
    const bool  uniform = std::all_of(vals.begin(), vals.end(), [&](const auto& kv) { return kv.second == vals.begin()->second; });
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
  } else {
    // A COMBINATIONAL linearized array (a multi-dimensional one; the flat branch
    // above took every 1-D case) is aggregate storage with no power-on value, so
    // its declare carries no initializer -- and tolg then scalar-replaces it into
    // a packed bus whose FIRST element store has no base to set_mask onto ("array
    // '…' is written before it has an initializer"). Seed it exactly the way the
    // Pyrope frontend does, with a separate whole-array store. The store must NOT
    // become a declare child -- that flips tolg to the Memory representation,
    // which a partial element write cannot use.
    //
    // The seed value is written `0sb?`, but it lands as a ZERO: tolg's
    // whole-array store maps `nil`/`0sb?` to integer 0 and broadcasts it
    // (upass_tolg.cpp, the array_scalar_views_ branch). So an element nobody
    // drives reads 0 here, where the 1-D flat branch above -- which puts the
    // poison on the DECLARE, a shape the packed-bus store has no equivalent of
    // -- keeps it X. That is an X-fidelity gap, not a wrong value: it can only
    // make the emitted design MORE defined than the Verilog source.
    auto sidx = builder_.add_child(Lnast_ntype::create_store());
    ln.add_child(sidx, Lnast_node::create_ref(name));
    ln.add_child(sidx, Lnast_node::create_const("0sb?"));
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

  // M7: a REG-driven bundle output port keeps its TUPLE io interface, but the
  // body's flop is a flat SHADOW reg (`<port>_q`): every body access of the
  // port symbol re-points to the shadow (today's flat output-reg lowering,
  // resets and all), and a per-field bridge drives the tuple leaves from the
  // shadow's q. The bundle_port_info_ entry is erased so body field accesses
  // route flat (bit-slices of the shadow) instead of tuple ops on the port.
  std::optional<Struct_info> bridge_si;
  std::string                bridge_port;
  bool                       flat_bridge = false;
  auto                       mint_shadow = [&]() {
    bridge_port        = lname_of(sym);
    std::string shadow = absl::StrCat(bridge_port, "_q");
    for (int n = 0; used_names_.contains(shadow); ++n) {
      shadow = absl::StrCat(bridge_port, "_q", n);
    }
    used_names_.insert(shadow);
    sym_lname_[&sym] = shadow;
  };
  auto plain_name = [&]() {
    if (sym.name.empty() || std::isdigit(static_cast<unsigned char>(sym.name.front())) != 0) {
      return false;
    }
    for (const char c : sym.name) {
      if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
        return false;
      }
    }
    return true;
  };
  if (auto bit = bundle_port_info_.find(&sym); bit != bundle_port_info_.end()) {
    bridge_si = std::move(bit->second);
    bundle_port_info_.erase(bit);
    mint_shadow();
  } else if (output_syms_.contains(&sym) && sym_lname_.contains(&sym) && !options_.struct_port_bundles
             && struct_port_bundle_ok(sym.getType()) && plain_name()) {
    // M7 parity: the FLAT (graphs) flow gives a reg-driven struct OUTPUT port
    // the SAME `<port>_q` shadow flop + comb bridge as the bundle flow. The
    // qualification is the shared TYPE-ONLY rule, so the pyrope emission and
    // the flat lg reference name this state IDENTICALLY — the LEC's tier-1
    // name pairing then ties the two flops' free (no-reset) initial values
    // (trans_top's f8_rom_response_o_q vs the ref's f8_rom_response_o: the
    // unpaired inits diverged and refuted at step 1 on all-zero inputs).
    flat_bridge = true;
    mint_shadow();
  }

  const auto& type = sym.getType();
  if (type.getCanonicalType().isUnpackedArray()) {
    declare_unpacked(sym, /*is_reg=*/true);
    return;
  }
  // A packed register array is named state, not an addressable memory merely
  // because it persists across cycles -- but a runtime-indexed PACKED 2-D reg
  // `[N-1:0][W-1:0]` (W>1) IS a register file:
  // declare it as a scalar-element MEMORY `reg name:[N]uW = nil` (mirrors the
  // non-flatten scalar branch of declare_unpacked) so element reads/writes route
  // through the __memory path and LEC against an equivalent Pyrope memory. A
  // plain flat flop (the fall-through below) cannot align with a memory node.
  if (packed_mem_regs_.contains(&sym)) {
    int64_t n = 0, lo = 0;
    int     w  = 0;
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
      // Verilog nonblocking memory reads see old (committed) contents, and that
      // value is DEFINED, not x: ordering="old" (fwd=0, undef=0).
      {
        auto aidx = builder_.add_child(Lnast_ntype::create_attr_set());
        ln.add_child(aidx, Lnast_node::create_ref(name));
        ln.add_child(aidx, Lnast_node::create_const("ordering"));
        ln.add_child(aidx, Lnast_node::create_const("\"old\""));
      }
      auto didx = builder_.add_child(Lnast_ntype::create_declare());
      ln.add_child(didx, Lnast_node::create_ref(name));
      auto tidx = ln.add_child(didx, Lnast_ntype::create_comp_type_array());
      emit_prim_type_int(tidx, w, sg);
      ln.add_child(tidx, Lnast_node::create_const(absl::StrCat("[", n, "]")));
      ln.add_child(didx, Lnast_node::create_const("reg"));
      // An async reset that loads a per-entry PATTERN (`spec_table <= '{33,…,0}`)
      // becomes the array's own tuple initializer. It cannot ride the scalar
      // `initial` attribute the async-reset lowering normally emits — one scalar
      // on `reg x:[N]uW` broadcasts, so the pattern would silently collapse to
      // its bottom lane. That lowering skips `initial` for these symbols.
      if (auto rit = array_reset_lanes_.find(&sym); rit != array_reset_lanes_.end()) {
        auto vidx = ln.add_child(didx, Lnast_ntype::create_tuple_add());
        for (const auto& lane : rit->second) {
          ln.add_child(vidx, Lnast_node::create_const(lane));
        }
        clear_pending_loc();
        return;
      }
      // Power-on contents from an `initial` block (same shape as declare_unpacked):
      // uniform fill -> scalar broadcast, per-entry fill -> tuple literal, else nil.
      if (auto iit = mem_init_vals_.find(&sym); iit != mem_init_vals_.end() && !iit->second.empty()) {
        const auto& vals = iit->second;
        const bool  uniform
            = std::all_of(vals.begin(), vals.end(), [&](const auto& kv) { return kv.second == vals.begin()->second; });
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
    emit_unsupported(sym.location,
                     "unsupported-var-type",
                     std::string("variable '") + std::string(sym.name) + "' has a non-integral type");
    return;
  }

  auto        ti   = tinfo(type);
  auto        name = lname_of(sym);
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
  // M7 bridge: tuple output leaves driven combinationally from the shadow
  // reg's q (order-free — a reg read by name is its committed value).
  if (bridge_si) {
    auto p = to_pattern(name, ti.bits, ti.is_signed);
    for (const auto& f : bridge_si->fields) {
      auto fv = extract_field(p, f.off, f.bits);
      if (f.is_signed) {
        fv = builder_.create_sext_stmts(fv, std::to_string(f.bits - 1));
      }
      emit_leaf_store(absl::StrCat(bridge_port, ".", f.name), fv);
    }
  } else if (flat_bridge) {
    // Flat-flow parity bridge: the whole port driven from the shadow's q.
    builder_.create_assign_stmts(bridge_port, name);
  }
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
  if (!type.isIntegral()) {
    emit_unsupported(sym.location,
                     "unsupported-var-type",
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
    // An UNSIGNED local's DECLARED range coexists with the x poison, which is
    // why it states its type: `0sb?` is the WIDTH-TAKING wildcard (see
    // uPass_runner::resolve_x_fill), so it fills exactly the declared width and
    // lands INSIDE the envelope the range states — where the old width-matched
    // `0ub????…` literal read as -1 to the range check.
    //
    // A SIGNED local declares NO range, because no initializer can match one.
    // The fill refuses a signed destination and there is nothing to fill TO:
    // Dlop has no bounded-width all-unknown signed value — the sign bit is
    // itself the unknown, so an honest x sign-extends without bound and
    // Dlop::get_bits() can only bound it (at 65). Narrowing the poison to the
    // declared width instead would make the sign bit a KNOWN 0: the net of a
    // signed local holds its SIGN-EXTENDED value (every store is fit_wrap'd,
    // every read is the plain net), so a non-negative `0ub????` pattern is the
    // silent 0 the poison exists to prevent. Declaring `s4` around the
    // unbounded unknown would state an envelope its own initializer cannot sit
    // in; the range comes back the day Dlop can carry a width-bounded signed
    // unknown. Sign is not lost by omitting it — the importer sexts at every
    // operation (fit_wrap).
    if (ti.is_signed) {
      builder_.create_declare_stmts(name, "mut", "", "");
    } else {
      builder_.create_declare_stmts(name, "mut", int_max_str(ti.bits, ti.is_signed), int_min_str(ti.bits, ti.is_signed));
    }
    builder_.create_assign_stmts(name, "0sb?");
  }
  clear_pending_loc();
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

bool Slang_context::struct_port_bundle_ok(const slang::ast::Type& t) {
  const auto& ct = t.getCanonicalType();
  if (ct.isPackedArray() && ct.isIntegral()) {
    // Keep an array port as one packed logical IO at the language boundary.
    // A downstream graph transformation may expand the port once it can
    // preserve/reaggregate the public interface provenance. Treating the array
    // itself as a struct-style field bundle here expanded every internal call
    // boundary and introduced cross-instance dependency cycles in full Minion.
    return false;
  }
  if (!ct.isStruct() || !ct.isIntegral()) {
    return false;  // packed struct only: the PORT TYPE itself (not union/enum/array-of-struct)
  }
  bool has_field = false;
  for (const auto& f : ct.as<slang::ast::PackedStructType>().membersOfType<slang::ast::FieldSymbol>()) {
    const auto& fct = f.getType().getCanonicalType();
    // A NESTED packed struct is flattened RECURSIVELY into dotted leaves
    // (`req.read_en`), so a field access lowers identically at every depth.
    // It used to disqualify the WHOLE port, which kept it a packed word — and
    // then writing field X while reading a DISJOINT field Y became a
    // self-reference on that one word, i.e. a FALSE combinational cycle in a
    // design that is acyclic field-by-field. That cost `pass.lec` three defs on
    // lhdsuite's minion (`vpu_tensorfma` plus the two parents instantiating
    // it). See lhdsuite fixme.md issue 1b and
    // lhd/tests/struct_nested_field_leaf_test.sh.
    if (fct.isStruct()) {
      if (!struct_port_bundle_ok(f.getType())) {
        return false;
      }
      has_field = true;
      continue;
    }
    if (!fct.isIntegral() || fct.getBitWidth() == 0) {
      return false;
    }
    // Every other integral field is ONE leaf, wide or narrow. That now
    // includes a multi-dim packed array, a packed array-of-struct, and a
    // packed union: each rides as a single WIDE leaf (its flat bit width) —
    // in-leaf selects stay field-relative slices and partial writes splice
    // via emit_bundle_port_rmw's per-leaf set_mask, so no new lowering is
    // needed. The point of NOT demoting the whole port over one such field:
    // a packed port is one pid, and writing field X while reading a DISJOINT
    // field Y then looks like a same-cycle self-loop — the false ring that
    // refused lhdsuite minion's id_vpu_ctrl / dcache_ctrl_resp buses. A leaf
    // with interior structure (union lanes, struct elements) keeps ONE pid;
    // if a ring threads through disjoint bits INSIDE it, the sim's bit-level
    // slice groups (graph/port_reach) take over from there.
    has_field = true;
  }
  return has_field;
}

// LEAF field list of a bundled packed-struct port. A nested packed struct is
// flattened RECURSIVELY: its children become dotted leaves (`req.read_en`) with
// ABSOLUTE bit offsets, so every depth behaves like a one-level field and the
// whole-struct reconstruct (which sums off/bits) is unchanged. Only leaves are
// emitted — an intermediate level is a name prefix, never an entry — so nothing
// double-counts. Gated by struct_port_bundle_ok, which vets the same nesting.
std::vector<Slang_context::Struct_info::Field> Slang_context::struct_port_fields(const slang::ast::Type& t) {
  std::vector<Struct_info::Field> out;
  const auto&                     ct = t.getCanonicalType();
  if (ct.isPackedArray()) {
    const auto*                     elem  = ct.getArrayElementType();
    auto                            ti    = tinfo(*elem);
    const int                       count = ti.bits > 0 ? static_cast<int>(ct.getBitWidth()) / ti.bits : 0;
    std::vector<Struct_info::Field> element_fields;
    if (elem->getCanonicalType().isStruct()) {
      element_fields = struct_port_fields(*elem);
    } else {
      element_fields.push_back({"", 0, ti.bits, ti.is_signed});
    }
    for (int lane = count - 1; lane >= 0; --lane) {
      for (const auto& ef : element_fields) {
        const auto name = ef.name.empty() ? absl::StrCat("e", lane) : absl::StrCat("e", lane, ".", ef.name);
        out.push_back({name, static_cast<int64_t>(lane) * ti.bits + ef.off, ef.bits, ef.is_signed});
      }
    }
    return out;
  }
  auto collect = [&out](auto&& self, const slang::ast::Type& ty, std::string_view prefix, int64_t base_off) -> void {
    const auto& st = ty.getCanonicalType().as<slang::ast::PackedStructType>();
    for (const auto& f : st.membersOfType<slang::ast::FieldSymbol>()) {
      std::string nm  = prefix.empty() ? std::string(f.name) : absl::StrCat(prefix, ".", f.name);
      const auto  off = base_off + static_cast<int64_t>(f.bitOffset);
      const auto& fct = f.getType().getCanonicalType();
      if (fct.isStruct() && fct.isIntegral()) {
        self(self, f.getType(), nm, off);
        continue;
      }
      auto fi = tinfo(f.getType());
      out.push_back({std::move(nm), off, fi.bits, fi.is_signed});
    }
  };
  collect(collect, t, "", 0);
  return out;
}

bool Slang_context::bundle_port_qualifies(const slang::ast::PortSymbol& port, std::string_view owner_def) const {
  if (!options_.struct_port_bundles) {
    return false;
  }
  // flat_top_io: only the TOP module's own ports go back to a packed bus, so
  // the emitted netlist is a drop-in replacement for the source module. A top
  // is never instantiated, so the instance-connection call site (which passes
  // no owner) is unaffected and def/instance stay consistent.
  if (options_.flat_top_io && !owner_def.empty() && top_defs_.count(std::string(owner_def)) != 0) {
    return false;
  }
  if (port.direction != slang::ast::ArgumentDirection::In && port.direction != slang::ast::ArgumentDirection::Out) {
    return false;  // inout/ref excluded
  }
  // The io leaves are `<port>.<field>` dotted names — an escaped (backticked)
  // port name cannot form them; keep such a port flat. Name-based but still
  // deterministic (no body uses consulted), so def and call sites agree.
  if (port.name.empty() || std::isdigit(static_cast<unsigned char>(port.name.front())) != 0) {
    return false;
  }
  for (const char c : port.name) {
    if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
      return false;
    }
  }
  return struct_port_bundle_ok(port.getType())
         || vector_bundle_ports_.contains(absl::StrCat(owner_def, "\x1f", std::string(port.name)));
}

const Slang_context::Struct_info* Slang_context::bundle_port_of(const slang::ast::Symbol& sym) const {
  auto it = bundle_port_info_.find(&sym);
  return it == bundle_port_info_.end() ? nullptr : &it->second;
}

std::string Slang_context::bundle_port_body_base(const slang::ast::Symbol& sym) {
  if (auto it = bundle_out_shadow_.find(&sym); it != bundle_out_shadow_.end()) {
    return it->second;
  }
  return lname_of(sym);
}

std::string Slang_context::read_bundle_port_whole(const slang::ast::ValueSymbol& sym) {
  // Reconstruct the packed value from the field leaves (the inverse of the
  // whole-port write decomposition): OR each leaf, shifted to its bit offset.
  auto it = bundle_port_info_.find(&sym);
  if (it == bundle_port_info_.end()) {
    return "0";
  }
  const auto  fields = it->second.fields;  // copy: builder calls can rehash the map
  auto        base   = bundle_port_body_base(sym);
  std::string acc;
  for (const auto& f : fields) {
    auto raw    = read_leaf(absl::StrCat(base, ".", f.name));
    auto placed = to_pattern(raw, f.bits, f.is_signed);
    if (f.off != 0) {
      placed = builder_.create_shl_stmts(placed, std::to_string(f.off));
    }
    acc = acc.empty() ? placed : builder_.create_bit_or_stmts({acc, placed});
  }
  return acc.empty() ? std::string{"0"} : acc;
}

const Slang_context::Struct_info::Field* Slang_context::find_struct_field(const Struct_info& si, std::string_view name) const {
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
  si.is_wire      = wire_syms_.contains(&sym);
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
  auto  base  = lname_of(sym);
  auto& ln    = *builder_.lnast;
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
      // SKIP the poison for a FUNCTION-scope struct (an inlined call's arg or
      // local): the arg is bound whole at the inline site, so the poison is
      // dead by construction — and when the call site sits in a unique_if
      // (case) arm, the dotted poison store survives the branch merge in a
      // field-store form tolg cannot lower (intpipe_csr_file's
      // read_fcsr_as_frm in a decode case arm).
      auto leaf = absl::StrCat(base, ".", std::string(f.name));
      builder_.create_declare_stmts(leaf, "mut", "", "");
      const auto* psc      = sym.getParentScope();
      const bool  fn_local = psc != nullptr && psc->asSymbol().kind == slang::ast::SymbolKind::Subroutine;
      if (!fn_local) {
        if (fi.is_signed) {
          builder_.create_assign_stmts(leaf, "0sb?");
        } else {
          std::string qmarks(static_cast<size_t>(fi.bits), '?');
          builder_.create_assign_stmts(leaf, absl::StrCat("0ub", qmarks));
        }
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

// Counts the number of STORE statements to each net (a case with one assign per
// arm is N stores; a bit-slice write chain `x#[a]=…; x#[b]=…` is N stores). A
// `wire` net that is stored more than once cannot be a single-driver wire and
// needs the mut+wire split (wire_split_tmp_).
struct Store_counter : public slang::ast::ASTVisitor<Store_counter, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_map<const slang::ast::ValueSymbol*, int>* counts  = nullptr;
  absl::flat_hash_set<const slang::ast::ValueSymbol*>*      partial = nullptr;  // any bit-slice/field/element store

  void note(const slang::ast::Expression& l) {
    const slang::ast::Expression* e = &l;
    while (e->kind == ExpressionKind::Conversion) {
      e = &e->as<slang::ast::ConversionExpression>().operand();
    }
    // A WHOLE store is a bare NamedValue/HierarchicalValue LHS; anything else
    // (RangeSelect/ElementSelect/MemberAccess) is a partial (set_mask) write —
    // it cannot be a single whole-driver wire on its own.
    const bool whole = e->kind == ExpressionKind::NamedValue || e->kind == ExpressionKind::HierarchicalValue;
    if (const auto* s = lhs_base_symbol(*e)) {
      ++(*counts)[s];
      if (!whole && partial != nullptr) {
        partial->insert(s);
      }
    }
  }

  void handle(const slang::ast::AssignmentExpression& expr) {
    const auto& lhs = expr.left();
    if (lhs.kind == ExpressionKind::Concatenation) {
      for (const auto* op : lhs.as<slang::ast::ConcatenationExpression>().operands()) {
        note(*op);
      }
    } else {
      note(lhs);
    }
    visitDefault(expr);
  }
};

// Per-driver def/use collection for the dependency sort. Reads are every
// NamedValue in rvalue position plus partial-write LHS bases (the RMW
// lowering reads them); full scalar-write LHS bases are NOT reads.
struct Dep_collector : public slang::ast::ASTVisitor<Dep_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*> reads;
  absl::flat_hash_set<const slang::ast::ValueSymbol*> writes;
  // GENUINE reads only — a value an expression actually reads (an rvalue, or a
  // selector index). note_lhs's base insert is NOT one of these: a partial
  // write records a base read purely because the ORDERED lowering spells
  // `x[0] = e` as `mut x__w1 = x; x__w1#[0] = e`. Telling the two apart is what
  // lets the driver-order check below see a driver that really does read bits
  // its SIBLINGS write.
  absl::flat_hash_set<const slang::ast::ValueSymbol*> rhs_reads;

  // Every SELECTOR nested along an lvalue PATH is a genuine rvalue read of its
  // index (`q[i][j].f = e` reads both `i` and `j`), while the path's own base is
  // a write target, not an rvalue. Descending with visit() would record the base
  // as an rhs_read; this walks the path and visits only the selectors. Missing
  // them is exactly the failure the MemberAccess rvalue handler below documents,
  // in its lvalue mirror: the index net ends up with no reader at all.
  void note_path_selectors(const slang::ast::Expression& e) {
    switch (e.kind) {
      case ExpressionKind::Conversion  : note_path_selectors(e.as<slang::ast::ConversionExpression>().operand()); return;
      case ExpressionKind::MemberAccess: note_path_selectors(e.as<slang::ast::MemberAccessExpression>().value()); return;
      case ExpressionKind::ElementSelect: {
        const auto& es = e.as<slang::ast::ElementSelectExpression>();
        es.selector().visit(*this);
        note_path_selectors(es.value());
        return;
      }
      case ExpressionKind::RangeSelect: {
        const auto& rs = e.as<slang::ast::RangeSelectExpression>();
        rs.left().visit(*this);
        rs.right().visit(*this);
        note_path_selectors(rs.value());
        return;
      }
      default: return;
    }
  }

  void note_lhs(const slang::ast::Expression& lhs) {
    switch (lhs.kind) {
      case ExpressionKind::NamedValue:
      case ExpressionKind::HierarchicalValue: writes.insert(&lhs.as<slang::ast::ValueExpressionBase>().symbol); return;
      case ExpressionKind::Conversion       : note_lhs(lhs.as<slang::ast::ConversionExpression>().operand()); return;
      case ExpressionKind::Concatenation:
        for (const auto* op : lhs.as<slang::ast::ConcatenationExpression>().operands()) {
          note_lhs(*op);
        }
        return;
      case ExpressionKind::ElementSelect: {
        const auto& es = lhs.as<slang::ast::ElementSelectExpression>();
        es.selector().visit(*this);
        note_path_selectors(es.value());  // `q[i][j] = e` also reads `i`
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
        note_path_selectors(rs.value());
        if (const auto* sym = lhs_base_symbol(rs.value())) {
          writes.insert(sym);
          reads.insert(sym);
        }
        return;
      }
      case ExpressionKind::MemberAccess: {
        note_path_selectors(lhs.as<slang::ast::MemberAccessExpression>().value());  // `q[i].f = e` reads `i`
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

  void handle(const slang::ast::VariableDeclStatement& stmt) {
    // Automatic locals inside a process are not separate module drivers, but
    // their initializer cones still belong to THIS process driver. Slang's
    // generic statement visitor does not descend through the declaration's
    // VariableSymbol initializer. Without doing so, a FIRRTL-style
    // `automatic t tmp = child_out.field; q <= tmp;` records only a read of
    // `tmp`; the producer instance is absent from the dependency graph and the
    // process may snapshot the aggregate before its driver is emitted.
    if (const auto* init = stmt.symbol.getInitializer()) {
      init->visit(*this);
    }
  }

  void handle(const slang::ast::MemberAccessExpression& expr) {
    // A member expression's ValueExpressionBase::symbol is the FIELD symbol,
    // while an instance output connection is recorded against the ROOT net.
    // Using the field here therefore loses the writer->reader dependency for
    // `child_out.bundle.field`: a sequential consumer can emit before the
    // child instance, snapshot the aggregate's poison initializer, and fold
    // the real child-output arm away on the Pyrope round trip.  Dependency
    // ordering is per storage object, so canonicalize member reads to the same
    // root symbol note_lhs() uses for writes.
    if (const auto* sym = lhs_base_symbol(expr)) {
      reads.insert(sym);
      rhs_reads.insert(sym);
    }
    // KEEP DESCENDING. Defining handle() REPLACES slang's default traversal for
    // this node, so returning here discards the whole sub-expression — and with
    // it every read nested in a SELECTOR. `data_i[lfsr_bin].tag` then recorded
    // only `data_i`, and `lfsr_bin` had no reader at all: the back-edge wire
    // classification never saw the early read, the net stayed a `0sb?`-poisoned
    // `mut`, its instance-output binding (`lfsr_bin = i_lfsr.refill_way_bin`)
    // emitted after every reader and was dropped as dead, and CVA6's
    // miss_handler read an X way-index forever (`data_i[lfsr_bin]`, 3 sites).
    expr.value().visit(*this);
  }

  void handle(const slang::ast::ValueExpressionBase& expr) {
    reads.insert(&expr.symbol);
    rhs_reads.insert(&expr.symbol);
  }
};

}  // namespace

// External nets connected to an instance's PURELY-STATE output ports — an
// output whose internal driver is a submodule flop/latch q (so it is
// combinationally independent of every input at the module boundary, exactly
// like a state q at this level). A read of such a net is order-free: it must
// NOT make the reader depend on the instance. Otherwise a pipeline-register
// feedback — a forward path reads
// `stage_reg.out`, whose `stage_reg.in = f(forwarded)` — is mistaken for a comb
// loop, and the cyclic-net `wire` fallback then routes the (now position-
// independent) read through the wrong driver (the forwarding mux), miscompiling
// every consumer of that stage register's output (e.g. io_dmem_address reading a
// don't-care instead of the ex/mem result).
static void collect_state_outputs(const slang::ast::InstanceSymbol&               inst,
                                  absl::flat_hash_set<const slang::ast::Symbol*>& ext_nets) {
  // The INSTANCE's own body (NOT getCanonicalBody) so the reg set computed below
  // and this instance's port.internalSymbol reference the same per-instance
  // symbols. With the canonical (first-instance) body, every non-first instance
  // — e.g. pipeB_* mirroring pipeA_* — would find no port match and keep its
  // false comb-loop back-edges.
  const auto* body = &inst.body;

  // (1) Submodule state set: vars written nonblocking in an edge-sensitive
  // always or an always_latch. A latch q is just as much a state cut here as a
  // flop q; omitting it made legal instance-output feedback bind the parent's
  // poison initializer instead (minion intpipe_mul_div_ctl/mdctl_dw_2q).
  absl::flat_hash_set<const slang::ast::Symbol*> regs;
  for (const auto& member : body->members()) {
    if (member.kind != SymbolKind::ProceduralBlock) {
      continue;
    }
    const auto& pbs           = member.as<slang::ast::ProceduralBlockSymbol>();
    bool        is_state_proc = pbs.procedureKind == slang::ast::ProceduralBlockKind::AlwaysLatch;
    if (pbs.procedureKind == slang::ast::ProceduralBlockKind::Always
        || pbs.procedureKind == slang::ast::ProceduralBlockKind::AlwaysFF) {
      const auto& stmt = pbs.getBody();
      if (stmt.kind == StatementKind::Timed) {
        const auto& timing = stmt.as<slang::ast::TimedStatement>().timing;
        auto        scan   = [&](const slang::ast::TimingControl& tc) {
          if (tc.kind == slang::ast::TimingControlKind::SignalEvent) {
            auto edge      = tc.as<slang::ast::SignalEventControl>().edge;
            is_state_proc |= edge == slang::ast::EdgeKind::PosEdge || edge == slang::ast::EdgeKind::NegEdge;
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
    if (!is_state_proc) {
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
    const slang::ast::Symbol*                           member = nullptr;
    std::string                                         prefix{};  // genblk name prefix at collection point
    absl::flat_hash_set<const slang::ast::ValueSymbol*> reads{};
    absl::flat_hash_set<const slang::ast::ValueSymbol*> writes{};
    // UninstantiatedDef (blackbox) only: inferred per-connection direction,
    // aligned with getPortConnections() (see the inference pass below).
    std::vector<bool>                                   bb_outs{};
    // Subset of `reads` that is a GENUINE read (see Dep_collector::rhs_reads).
    // Left empty for driver kinds that do not collect it, which keeps them out
    // of the driver-order check below.
    absl::flat_hash_set<const slang::ast::ValueSymbol*> rhs_reads{};
    // True when this driver was elaborated from a generate loop. Multiple
    // partial continuous assignments only have the unsupported cross-iteration
    // ordering ambiguity in that context; ordinary source-level split drivers
    // retain their source order.
    bool                                                in_generate_loop = false;
  };
  // Every set/vector default-constructs empty, so a construction site names only
  // `member` and `prefix` -- adding a field must not mean editing five brace
  // lists again (this struct grew one and all five had to change).
  std::vector<Driver>                            drivers;
  std::vector<size_t>                            unknown_idx;  // drivers[] entries that are blackbox instances
  // External nets driven by pure flop/latch state outputs; reads of them are
  // order-free (see collect_state_outputs) so pass 2 skips their back-edges.
  absl::flat_hash_set<const slang::ast::Symbol*> seq_out_nets;
  // Ordinary (non-generated) partial continuous drivers that read sibling
  // slices of the same net. Their RHS must read the resolved wire while their
  // LHS writes the split accumulator; see emit_driver below.
  absl::flat_hash_set<const slang::ast::ValueSymbol*> resolved_cont_selfrefs;
  size_t                                              generate_loop_depth = 0;

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
        case SymbolKind::StatementBlock:  // lowered where referenced (slang puts them next to procedures)
        case SymbolKind::Subroutine:      // bodies fold at call sites or are diagnosed there
        case SymbolKind::ElabSystemTask:  // $info/$warning/$error handled by slang itself
        case SymbolKind::WildcardImport:  // `import pkg::*` — slang already resolved the names
        case SymbolKind::ExplicitImport:  // `import pkg::sym` — ditto
        case SymbolKind::Modport:         // interface modport view; not codegen-relevant here
        case SymbolKind::AssertionPort:   // property/sequence formal args
        case SymbolKind::Sequence:        // named sequences (assertion-only, not synthesized)
        case SymbolKind::Property:        // named properties (assertion-only, not synthesized)
          break;

        case SymbolKind::Net: {
          const auto& ns = member.as<slang::ast::NetSymbol>();
          if (const auto* expr = ns.getInitializer()) {
            Driver        d{.member = &member, .prefix = genblk_prefix_};
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
            emit_warning(slang::SourceRange(vs.location, vs.location),
                         "var-init-ignored",
                         "unsupported",
                         std::string("initializer of '") + std::string(vs.name) + "' is ignored (initial-block semantics)");
          }
          break;
        }

        case SymbolKind::ContinuousAssign: {
          Driver        d{.member = &member, .prefix = genblk_prefix_};
          Dep_collector dc;
          member.as<slang::ast::ContinuousAssignSymbol>().getAssignment().visit(dc);
          d.reads            = std::move(dc.reads);
          d.writes           = std::move(dc.writes);
          d.rhs_reads        = std::move(dc.rhs_reads);
          d.in_generate_loop = generate_loop_depth != 0;
          drivers.push_back(std::move(d));
          break;
        }

        case SymbolKind::ProceduralBlock: {
          Driver        d{.member = &member, .prefix = genblk_prefix_};
          Dep_collector dc;
          member.as<slang::ast::ProceduralBlockSymbol>().getBody().visit(dc);
          d.reads     = std::move(dc.reads);
          d.writes    = std::move(dc.writes);
          d.rhs_reads = std::move(dc.rhs_reads);
          drivers.push_back(std::move(d));
          break;
        }

        case SymbolKind::Instance: {
          collect_state_outputs(member.as<slang::ast::InstanceSymbol>(), seq_out_nets);
          Driver d{.member = &member, .prefix = genblk_prefix_};
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
          ++generate_loop_depth;
          for (const auto* entry : arr.entries) {
            std::string idx_txt
                = entry->arrayIndex != nullptr ? entry->arrayIndex->toString() : std::to_string(entry->constructIndex);
            genblk_prefix_ = absl::StrCat(saved_prefix, arr.getExternalName(), "_", idx_txt, "_");
            collect(*entry);
          }
          --generate_loop_depth;
          genblk_prefix_ = saved_prefix;
          break;
        }

        case SymbolKind::UninstantiatedDef: {
          // Unknown-module instance. Under a USER --ignore-unknown-modules it
          // is kept as an opaque blackbox sub-instance (slang has no port
          // directions for it, so reads/writes are filled by the inference
          // pass below); otherwise a typo'd module name stays a clean error.
          const auto& ud = member.as<slang::ast::UninstantiatedDefSymbol>();
          if (!options_.blackbox_unknown || ud.isChecker()) {
            emit_unsupported(member.location,
                             "unknown-module",
                             std::string("instance '") + std::string(member.name)
                                 + "' refers to an unknown module (no definition in this compile)",
                             "provide the module source, or pass --ignore-unknown-modules to keep it as a blackbox "
                             "instance (pyrope emission imports it)");
            break;
          }
          drivers.push_back(Driver{.member = &member, .prefix = genblk_prefix_});
          unknown_idx.push_back(drivers.size() - 1);
          break;
        }

        default:
          emit_unsupported(member.location,
                           "unsupported-member",
                           std::string("module member '") + std::string(member.name) + "' (kind "
                               + std::string(slang::ast::toString(member.kind)) + ") is not supported by --reader slang");
      }
    }
  };
  collect(scope);

  // ── pass 1b: blackbox port-direction inference ─────────────────────────────
  // An unknown module gives slang no port directions, so infer them from how
  // the module uses the connected net:
  //   input  (confident) — a computed rvalue, a module input, or a net WRITTEN
  //                        by known logic (assign/process/net-init/known
  //                        instance output);
  //   output (confident) — an undriven lvalue net that known logic READS or
  //                        that goes to a module output;
  //   ambiguous          — neither written nor read by anything known, incl. a
  //                        net between two blackboxes: the direction cannot be
  //                        guessed, so warn and assume (first blackbox in
  //                        source order drives it, later ones read it).
  if (!unknown_idx.empty()) {
    absl::flat_hash_set<const slang::ast::ValueSymbol*> driven;         // written by known logic
    absl::flat_hash_set<const slang::ast::ValueSymbol*> read_by_known;  // read by known logic
    for (const auto& d : drivers) {                                     // blackbox drivers are still empty here
      driven.insert(d.writes.begin(), d.writes.end());
      read_by_known.insert(d.reads.begin(), d.reads.end());
    }
    // Nets an earlier blackbox claimed as its output (the ambiguous-guess
    // trail), mapped to that blackbox for the warning text.
    absl::flat_hash_map<const slang::ast::ValueSymbol*, const slang::ast::UninstantiatedDefSymbol*> bb_claimed;

    // All lvalue root symbols of a connection expr, or false for a shape that
    // cannot be written (a computed rvalue — definitely an input).
    std::function<bool(const slang::ast::Expression&, std::vector<const slang::ast::ValueSymbol*>&)> lvalue_roots
        = [&](const slang::ast::Expression& e, std::vector<const slang::ast::ValueSymbol*>& roots) -> bool {
      if (e.kind == ExpressionKind::Concatenation) {
        for (const auto* op : e.as<slang::ast::ConcatenationExpression>().operands()) {
          if (!lvalue_roots(*op, roots)) {
            return false;
          }
        }
        return true;
      }
      const auto* sym = lhs_base_symbol(e);
      if (sym == nullptr) {
        return false;
      }
      roots.push_back(sym);
      return true;
    };

    for (size_t k : unknown_idx) {
      auto&       d     = drivers[k];
      const auto& ud    = d.member->as<slang::ast::UninstantiatedDefSymbol>();
      auto        conns = ud.getPortConnections();
      auto        names = ud.getPortNames();
      d.bb_outs.assign(conns.size(), false);
      for (size_t i = 0; i < conns.size(); ++i) {
        const auto* pe = unknown_conn_expr(conns[i]);
        if (pe == nullptr) {
          continue;
        }
        const auto&                                 expr = *pe;
        std::vector<const slang::ast::ValueSymbol*> roots;
        const bool                                  is_lvalue = lvalue_roots(expr, roots) && !roots.empty();

        const std::string pname     = i < names.size() && !names[i].empty() ? std::string(names[i]) : std::to_string(i);
        auto              conn_desc = [&]() {
          return std::string("port '") + pname + "' of blackbox instance '" + std::string(ud.name) + "' ('"
                 + std::string(ud.definitionName) + "')";
        };

        bool                                       in_conf  = !is_lvalue;  // a computed rvalue can only be an input
        bool                                       out_conf = false;
        const slang::ast::UninstantiatedDefSymbol* bb_peer  = nullptr;
        for (const auto* r : roots) {
          if (input_syms_.contains(r) || driven.contains(r)) {
            in_conf = true;
          } else if (auto it = bb_claimed.find(r); it != bb_claimed.end()) {
            bb_peer = it->second;
          } else if (read_by_known.contains(r) || output_syms_.contains(r)) {
            out_conf = true;
          }
        }

        bool is_out;
        if (in_conf) {  // a written net cannot be a blackbox output
          is_out = false;
          if (out_conf) {
            emit_warning(expr.sourceRange,
                         "unknown-module-dir-guess",
                         "io",
                         "mixed direction evidence for " + conn_desc()
                             + " (parts of the connection are written, parts only read) — assuming an input");
          }
        } else if (bb_peer != nullptr) {
          is_out = false;
          emit_warning(expr.sourceRange,
                       "unknown-module-dir-guess",
                       "io",
                       "direction of " + conn_desc() + " cannot be inferred: the net only connects blackboxes — assuming '"
                           + std::string(bb_peer->definitionName) + "' (instance '" + std::string(bb_peer->name)
                           + "') drives it and this is an input");
        } else if (out_conf) {
          is_out = true;
        } else {
          is_out = true;  // undriven and unread: nothing known to contradict either way
          emit_warning(expr.sourceRange, "unknown-module-dir-guess", "io",
                       "direction of " + conn_desc()
                           + " cannot be inferred (the net is neither driven nor read in this module) — assuming an "
                             "output");
        }

        if (is_out) {
          d.bb_outs[i] = true;
          Dep_collector dc;
          dc.note_lhs(expr);  // writes the roots; select indices become reads
          d.reads.insert(dc.reads.begin(), dc.reads.end());
          d.writes.insert(dc.writes.begin(), dc.writes.end());
          for (const auto* r : roots) {
            bb_claimed.emplace(r, &ud);
          }
        } else {
          Dep_collector dc;
          expr.visit(dc);
          d.reads.insert(dc.reads.begin(), dc.reads.end());
        }
      }
    }
  }

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
  // ── wire classification: BACK-EDGE ONLY ────────────────────────────────────
  // A net needs `wire` (single-driver, position-independent read) ONLY when one
  // of its readers emits BEFORE its first writer in the final emission order
  // (`order` then `cyclic`) — a genuine read-before-write. Every other net is
  // written before it is read, so it stays `mut`/`const`.
  //
  // This replaces the old "every net written by ANY cyclic driver becomes a
  // wire". Instances are modeled coarsely (one node, every output assumed to
  // depend on every input), so a datapath of always-blocks + instances fuses
  // into one big false SCC; the old rule then promoted the WHOLE SCC to `wire`
  // (txfma_e2: 16 nets, incl. `e_1_tk_f2a_h` which is single-writer and written
  // before its only reader). The back-edge test keeps `wire` for exactly the
  // true feedback nets and drops the rest to `mut`. State instance outputs
  // (seq_out_nets) fold in: their deps back-edges were dropped so the topo order
  // may place a reader first — the same position check catches precisely those,
  // instead of blanket-wiring every registered output.
  {
    std::vector<size_t> pos(drivers.size(), 0);
    size_t              p = 0;
    for (size_t i : order) {
      pos[i] = p++;
    }
    for (size_t i : cyclic) {
      pos[i] = p++;
    }
    absl::flat_hash_map<const slang::ast::ValueSymbol*, std::vector<size_t>> readers_of;
    for (size_t i = 0; i < drivers.size(); ++i) {
      for (const auto* r : drivers[i].reads) {
        readers_of[r].push_back(i);
      }
    }
    for (const auto& [net, ws] : writers_of) {
      if (net == nullptr || reg_syms_.contains(net) || input_syms_.contains(net)) {
        continue;
      }
      // Only a MODULE-LEVEL net can be a wire; a procedural-block-local var has
      // no stable cut driver and keeps its `mut` poison-init.
      const auto* sc = net->getParentScope();
      const bool  module_level
          = sc != nullptr && (&sc->asSymbol() == body_ || sc->asSymbol().kind == slang::ast::SymbolKind::GenerateBlock);
      if (!module_level) {
        continue;
      }
      size_t wpos = SIZE_MAX;
      for (size_t w : ws) {
        wpos = std::min(wpos, pos[w]);
      }
      auto rit = readers_of.find(net);
      if (rit == readers_of.end()) {
        continue;  // never read → no read-before-write
      }
      for (size_t r : rit->second) {
        // A driver that also writes `net` normally reads its own prior/poison
        // value intra-block (RMW), which is not a cross-driver early read.
        // There is one important exception: an INSTANCE may feed one of its
        // pure flop/latch outputs back into one of its inputs. The output
        // is a state-q cut, so this is not a combinational loop, and the input
        // must read the resolved instance-output net.  Leaving it as a `mut`
        // snapshots the poison initializer while lower_instance is building
        // the input arguments; the later output binding cannot repair that
        // already-created SSA value (minion intpipe_mul_div_ctl's
        // mdctl_dw_2q feedback became a permanent `0ub?`).  A `wire` buffer is
        // exactly the position-independent representation for this legal
        // state feedback.
        if (drivers[r].writes.contains(net)) {
          if (seq_out_nets.contains(net)) {
            wire_syms_.insert(net);
            continue;  // NOT `break`: a LATER reader may still be a resolvable split driver
          }
          if (drivers[r].member != nullptr && drivers[r].member->kind == SymbolKind::ContinuousAssign
              && !drivers[r].in_generate_loop && drivers[r].rhs_reads.contains(net) && ws.size() > 1) {
            const auto& asg = drivers[r].member->as<slang::ast::ContinuousAssignSymbol>().getAssignment();
            if (asg.kind == ExpressionKind::Assignment) {
              const auto* lhs = &asg.as<slang::ast::AssignmentExpression>().left();
              while (lhs->kind == ExpressionKind::Conversion) {
                lhs = &lhs->as<slang::ast::ConversionExpression>().operand();
              }
              if (lhs->kind != ExpressionKind::NamedValue && lhs->kind != ExpressionKind::HierarchicalValue) {
                // Continuous assigns are concurrent. Represent an ordinary
                // split net as one resolved wire around a mut accumulator so
                // every RHS observes the complete set of sibling drivers.
                wire_syms_.insert(net);
                resolved_cont_selfrefs.insert(net);
                break;
              }
            }
          }
          continue;
        }
        // A plain early read only needs `wire`; keep scanning, because a LATER
        // reader may be the split self-referential continuous driver that also
        // needs `resolved_cont_selfrefs`. Bailing out here on the FIRST match
        // made the classification depend on driver index order and silently
        // left those drivers on the old source-ordered (miscompiling) path.
        if (pos[r] < wpos) {
          wire_syms_.insert(net);
          continue;
        }
      }
    }

    // ── REFUSE a driver order that reads a net before anyone writes it ──────
    //
    // Continuous assigns are ORDER-FREE in Verilog; this reader has to
    // linearize them. Pass 2 above deliberately drops EVERY ordering edge of a
    // driver that reads the net it writes (`i_writes_r`), because at SYMBOL
    // granularity two partial writers reading each other's bits look like a
    // cycle. Source order then decides — and a generate loop emitting a
    // reduction tree ROOT-first is exactly backwards:
    //
    //   for (genvar level = 0; level < NumLevels; level++)          // lzc.sv:74
    //     assign sel_nodes[2**level-1+l] = sel_nodes[2**(level+1)-1+l*2] | ...;
    //
    // The root reads sibling bits no driver has written yet, takes the
    // multi-writer net's `0sb?` seed, and the whole cone folds to X. That used
    // to ship SILENTLY at exit 0 with no diagnostic: six CVA6 modules
    // (lzc_p1..p7) had EVERY output an x constant, twenty-six had at least one.
    // A wrong netlist is far worse than a refused one.
    //
    // The test is exact and needs no bit ranges: a driver that BOTH genuinely
    // reads and writes the net, emitted at or before the FIRST driver that
    // writes it, is by construction reading bits nobody has written. An
    // ordinary RMW chain (`assign x[0] = a; assign x[1] = x[0];`) has its
    // reader strictly after the first writer and is untouched; a non-writing
    // early reader is the back-edge `wire` case handled just above; and a legal
    // instance state-feedback net (seq_out_nets) is excluded with it.
    for (const auto& [net, ws] : writers_of) {
      if (net == nullptr || ws.size() < 2 || reg_syms_.contains(net) || input_syms_.contains(net) || seq_out_nets.contains(net)
          || wire_syms_.contains(net)) {
        continue;
      }
      size_t first_write = SIZE_MAX;
      for (size_t w : ws) {
        first_write = std::min(first_write, pos[w]);
      }
      for (size_t w : ws) {
        // ONLY a continuous assign. A procedural block ORDERS ITS OWN
        // statements, so a process that reads and writes one net is just an
        // ordinary sequential read — two `always` blocks sharing an `integer i`
        // loop counter is the common shape, and flagging it is a false alarm.
        // A continuous assign has no internal order, so a self-read there is
        // genuinely a read of what a SIBLING driver produces.
        if (drivers[w].member == nullptr || drivers[w].member->kind != SymbolKind::ContinuousAssign
            || !drivers[w].in_generate_loop) {
          continue;
        }
        if (pos[w] > first_write || !drivers[w].rhs_reads.contains(net)) {
          continue;
        }
        // ...and only a PARTIAL (slice) driver. The failure is several drivers
        // splitting one net by bits and reading each other's slices; a driver
        // that writes the WHOLE net cannot be reading a slice a sibling owns,
        // and its own read is overwritten by whatever writes the net last
        // (`assign _T_619 = _T_619;`, a firrtl self-copy tautology, is the
        // shape that makes this matter).
        {
          const auto& asg = drivers[w].member->as<slang::ast::ContinuousAssignSymbol>().getAssignment();
          if (asg.kind != slang::ast::ExpressionKind::Assignment) {
            continue;
          }
          const auto* lhs = &asg.as<slang::ast::AssignmentExpression>().left();
          while (lhs->kind == ExpressionKind::Conversion) {
            lhs = &lhs->as<slang::ast::ConversionExpression>().operand();
          }
          if (lhs->kind == ExpressionKind::NamedValue || lhs->kind == ExpressionKind::HierarchicalValue) {
            continue;  // whole-net driver
          }
        }
        const auto* member = drivers[w].member;
        emit_unsupported(member != nullptr ? slang::SourceRange(member->location, member->location) : slang::SourceRange(),
                         "unsupported-driver-order",
                         std::string("--reader slang cannot order generated partial drivers of '") + std::string(net->name)
                             + "': a continuous assignment inside a generate loop reads and writes the net before another "
                               "iteration has written the slices it reads",
                         "give each generated level its own net, arrange the generated assignments so every slice is "
                         "written before it is read, or move the split assignments out of the generate loop");
        break;  // one refusal per net is enough
      }
    }
  }

  // ── edge-sensitivity nets are wires ────────────────────────────────────────
  // A DERIVED net named in an always_ff edge list (`negedge rst_int_ni`, a
  // gated clock) becomes a reg ATTR reference (`reset_pin=ref net`,
  // `clock_pin=ref net`). Attr semantics are net-like — the ref must bind the
  // RESOLVED value, never an SSA version. As a `mut` the net carries a poison
  // store plus its driver store, and the attr ref downstream binds the FIRST
  // version — prim_rst_sync's reset_pin resolved to the poison and the flop
  // lost its async reset entirely (`if (2'sb0?)` in the lowered Verilog, only
  // on the GRAPHS flow: the pyrope round-trip happened to re-declare the net
  // `wire`). Classify every such net a `wire` (single-driver; the split below
  // handles a multiply-written one), so both flows carry the same
  // unambiguous single-name net.
  {
    std::function<void(const slang::ast::Scope&)> scan_edges = [&](const slang::ast::Scope& sc) {
      for (const auto& member : sc.members()) {
        if (member.kind == SymbolKind::GenerateBlock) {
          const auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
          if (!gen.isUninstantiated) {
            scan_edges(gen);
          }
          continue;
        }
        if (member.kind == SymbolKind::GenerateBlockArray) {
          for (const auto* entry : member.as<slang::ast::GenerateBlockArraySymbol>().entries) {
            scan_edges(*entry);
          }
          continue;
        }
        if (member.kind != SymbolKind::ProceduralBlock) {
          continue;
        }
        const auto& pbs = member.as<slang::ast::ProceduralBlockSymbol>();
        if (pbs.procedureKind != slang::ast::ProceduralBlockKind::Always
            && pbs.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF) {
          continue;
        }
        const auto& stmt = pbs.getBody();
        if (stmt.kind != StatementKind::Timed) {
          continue;
        }
        auto note_edge_net = [&](const slang::ast::TimingControl& tc) {
          if (tc.kind != slang::ast::TimingControlKind::SignalEvent) {
            return;
          }
          const auto& sev = tc.as<slang::ast::SignalEventControl>();
          if (sev.edge != slang::ast::EdgeKind::PosEdge && sev.edge != slang::ast::EdgeKind::NegEdge) {
            return;
          }
          if (sev.expr.kind != ExpressionKind::NamedValue) {
            return;
          }
          const auto& sym = sev.expr.as<slang::ast::NamedValueExpression>().symbol;
          if (input_syms_.contains(&sym) || reg_syms_.contains(&sym) || declared_.contains(&sym) || wire_syms_.contains(&sym)) {
            return;  // ports/regs are already order-free names; pre-declared nets keep their form
          }
          const auto& ct = sym.getType().getCanonicalType();
          if (!ct.isIntegral() || ct.isStruct() || ct.isPackedUnion()) {
            return;  // plain scalar nets only (an edge source is 1-bit in practice)
          }
          const auto* psc = sym.getParentScope();
          const bool  module_level
              = psc != nullptr && (&psc->asSymbol() == body_ || psc->asSymbol().kind == slang::ast::SymbolKind::GenerateBlock);
          if (module_level) {
            wire_syms_.insert(&sym);
          }
        };
        const auto& timing = stmt.as<slang::ast::TimedStatement>().timing;
        if (timing.kind == slang::ast::TimingControlKind::EventList) {
          for (const auto* ev : timing.as<slang::ast::EventListControl>().events) {
            note_edge_net(*ev);
          }
        } else {
          note_edge_net(timing);
        }
      }
    };
    scan_edges(scope);
  }

  // ── wire SPLIT setup: a MULTIPLY-written wire needs a mut accumulator ───────
  // A wire net stored more than once cannot be a single-driver wire. Split it:
  // writes accumulate into `mut <net>__wtmp` (program-order last-wins), a final
  // `<net> = <net>__wtmp` bridge is the wire's ONE driver, and cross-driver
  // reads through the wire see the resolved value. A wire stored at most once
  // keeps the plain single-driver form (a continuous-assign / one instance
  // output / one store).
  //
  // These three live OUTSIDE the block: the struct pre-declare further down
  // reuses the same "is a plain single-driver wire valid here?" test.
  absl::flat_hash_map<const slang::ast::ValueSymbol*, int> store_counts;
  absl::flat_hash_set<const slang::ast::ValueSymbol*>      partial_writes;
  absl::flat_hash_set<const slang::ast::ValueSymbol*>      proc_written;  // written by an always block
  absl::flat_hash_set<const slang::ast::ValueSymbol*>      definite_proc_written;
  {
    for (const auto& d : drivers) {
      Store_counter sc;
      sc.counts  = &store_counts;
      sc.partial = &partial_writes;
      if (d.member->kind == SymbolKind::ProceduralBlock) {
        // A procedural write can be CONDITIONAL (an `if` with no covering else
        // leaves the net undriven on some path) — a plain wire is then
        // "incompletely driven". Splitting gives the accumulator a poison init
        // that supplies X on the uncovered path, exactly like a comb `mut`.
        proc_written.insert(d.writes.begin(), d.writes.end());
        const auto& pbs = d.member->as<slang::ast::ProceduralBlockSymbol>();
        pbs.getBody().visit(sc);
        // STRICT: the wire promotion below treats "definitely written" as a
        // licence to drop the accumulator, so it needs the proof, not the
        // latch analysis' bias-to-combinational guess (which calls a store
        // under an unmodelled statement — a loop, a `case … matches` — definite).
        definite_blocking_writes(pbs.getBody(), definite_proc_written, /*strict=*/true);
      } else if (d.member->kind == SymbolKind::ContinuousAssign) {
        d.member->as<slang::ast::ContinuousAssignSymbol>().getAssignment().visit(sc);
      } else if (d.member->kind == SymbolKind::Instance) {
        // An instance OUTPUT wired through a slice/select (`.p(net[2:0])`) is a
        // partial (set_mask) driver of `net`, so `net` cannot be a plain
        // single-driver wire — split it (the Store_counter scan above does not
        // see instance connections).
        for (const auto* conn : d.member->as<slang::ast::InstanceSymbol>().getPortConnections()) {
          const auto* expr = conn->getExpression();
          if (expr == nullptr || conn->port.kind != SymbolKind::Port
              || conn->port.as<slang::ast::PortSymbol>().direction != slang::ast::ArgumentDirection::Out) {
            continue;
          }
          const auto* t = expr;
          if (const auto* a = t->as_if<slang::ast::AssignmentExpression>()) {
            t = &a->left();
          }
          while (t->kind == ExpressionKind::Conversion) {
            t = &t->as<slang::ast::ConversionExpression>().operand();
          }
          if (t->kind != ExpressionKind::NamedValue && t->kind != ExpressionKind::HierarchicalValue) {
            if (const auto* s = lhs_base_symbol(*t)) {
              partial_writes.insert(s);
            }
          }
        }
      }
    }
    // A synthesis-style always_comb is a simultaneous combinational equation
    // system, even when cgen's stable node order prints a temporary's consumer
    // before its producer in the block.  Model a scalar that is both read and
    // written by one procedure as a resolved wire when the procedure proves one
    // unconditional whole write.  Otherwise the early read binds to the mut's
    // poison initializer and stays X forever on the Verilog -> Pyrope round
    // trip (ExeUnitImp_4: `_Alu_io_in_ready = get_mask; get_mask = Alu.ready`).
    //
    // The strict definite-write and single-store guards preserve procedural
    // accumulator semantics: conditional, partial, and multiply-written values
    // still use the split mut below.  A true `x = x + 1` becomes an honest
    // combinational wire loop and is refused downstream instead of silently
    // reading an invented previous activation.
    for (const auto& d : drivers) {
      if (d.member->kind != SymbolKind::ProceduralBlock) {
        continue;
      }
      for (const auto* sym : d.reads) {
        if (sym == nullptr || !d.writes.contains(sym) || !definite_proc_written.contains(sym) || partial_writes.contains(sym)) {
          continue;
        }
        auto scit = store_counts.find(sym);
        if (scit == store_counts.end() || scit->second != 1 || reg_syms_.contains(sym) || input_syms_.contains(sym)) {
          continue;
        }
        const auto* psc = sym->getParentScope();
        const bool  module_level
            = psc != nullptr && (&psc->asSymbol() == body_ || psc->asSymbol().kind == slang::ast::SymbolKind::GenerateBlock);
        const auto& ct = sym->getType().getCanonicalType();
        if (module_level && ct.isIntegral() && !ct.isStruct() && !ct.isPackedUnion() && !is_scalar_struct_var(*sym)
            && !flat_port_syms_.contains(sym) && !mem_syms_.contains(sym)) {
          wire_syms_.insert(sym);
        }
      }
    }
    // wire_syms_ is pointer-keyed: linearize before this loop mints `__wtmp`
    // names — the order decides `lname_of`'s `_sN` numbering.
    for (const auto* wsym : emit_ordered(wire_syms_)) {
      const auto* vs = wsym->as_if<slang::ast::ValueSymbol>();
      if (vs == nullptr) {
        continue;
      }
      // The split is a PLAIN-SCALAR device (a `mut` accumulator with a scalar
      // poison, whole/bit-slice writes). Skip anything with FIELDS or its own
      // machinery: a struct/union (per-field bundle OR flat-bus — both take
      // whole+field writes a scalar poison would mistype), a bundle port, a
      // flat-port/memory net, or a non-integral type.
      const auto& ct = vs->getType().getCanonicalType();
      if (ct.isStruct() || ct.isPackedUnion() || !ct.isIntegral() || is_scalar_struct_var(*vs) || bundle_port_of(*vs) != nullptr
          || flat_port_syms_.contains(vs) || mem_syms_.contains(vs)) {
        continue;
      }
      // Split when the net has more than one DRIVER (co-writers across blocks,
      // or an instance output plus a proc — writers_of counts every driver kind,
      // which the store-counter's proc/assign-only scan misses) OR more than one
      // STORE within a driver (a case + priority-if / bit-slice chain). Either
      // way a single-driver wire is impossible.
      auto      wit          = writers_of.find(vs);
      const int driver_count = wit != writers_of.end() ? static_cast<int>(wit->second.size()) : 0;
      auto      scit         = store_counts.find(vs);
      const int store_count  = scit != store_counts.end() ? scit->second : 0;
      if (driver_count <= 1 && store_count <= 1 && !partial_writes.contains(vs)
          && (!proc_written.contains(vs) || definite_proc_written.contains(vs))) {
        continue;  // one non-procedural driver, one WHOLE store: a plain wire is fine
      }
      // Readable, unique accumulator name derived from the wire's lname.
      std::string stem = lname_of(*vs);
      if (!stem.empty() && stem.front() == '`') {
        stem = stem.substr(1, stem.size() - 2);
      }
      // Unique against the RAW spelling: `used_names_` holds pre-quote names
      // (lname_of inserts before quote_if_needed), so uniquing on the quoted
      // form would neither see a real collision nor be visible to a later
      // lname_of. Quote only the name that actually goes out as an LNAST ref.
      std::string raw = absl::StrCat(stem, "__wtmp");
      for (int n = 0; used_names_.contains(raw); ++n) {
        raw = absl::StrCat(stem, "__wtmp", n);
      }
      used_names_.insert(raw);
      wire_split_tmp_[wsym] = ref_name_of_raw(raw);
    }
    // FLATTENED-AGGREGATE split: a wire-classified local whose representation
    // is a single flattened MUT bus (declare_unpacked's flatten branch:
    // flat_port_syms_ AND mem_syms_, pre-declared in lower_module BEFORE this
    // classification could run). The scalar loop above skipped it, but its
    // reads are exactly as position-dependent as a scalar's: a merge driver
    // sorted before the writers reads the bus's INITIAL value
    // (miss_handler_unit's `writeback_req_o |= mh_wb_req[i]` read mh_wb_req
    // before the child instances wrote it). The existing mut IS the
    // accumulator; readers are re-pointed to a fresh `<name>__wnet` wire whose
    // single driver is the end-of-module bridge, and a WRITER driver's
    // emission swaps back to the mut (the generic wire_split_tmp_ machinery,
    // with the split's naming inverted — the pre-declared mut cannot become
    // the wire). Split unconditionally: the bridge is always the wire's one
    // driver, so the driver/store-count refinements don't apply.
    for (const auto* wsym : emit_ordered(wire_syms_)) {
      const auto* vs = wsym->as_if<slang::ast::ValueSymbol>();
      if (vs == nullptr || wire_split_tmp_.contains(wsym) || !declared_.contains(wsym)) {
        continue;
      }
      if (!(flat_port_syms_.contains(vs) && mem_syms_.contains(vs) && mem_info_.contains(vs)) || input_syms_.contains(vs)
          || output_syms_.contains(vs)) {
        continue;  // only local flattened buses (ports/memories keep their paths)
      }
      std::string orig = lname_of(*vs);
      std::string stem = orig;
      if (!stem.empty() && stem.front() == '`') {
        stem = stem.substr(1, stem.size() - 2);
      }
      // Unique against the RAW spelling (see the __wtmp note above): quoting
      // before the collision check hides real collisions from lname_of.
      std::string wnet = absl::StrCat(stem, "__wnet");
      for (int n = 0; used_names_.contains(wnet); ++n) {
        wnet = absl::StrCat(stem, "__wnet", n);
      }
      used_names_.insert(wnet);
      wire_split_tmp_[wsym] = std::move(orig);  // accumulator = the pre-declared mut
      wire_split_flat_.insert(wsym);
      sym_lname_[wsym] = ref_name_of_raw(wnet);  // readers resolve through the wire
    }
  }

  // ── struct pre-declare (moved down from lower_module) ──────────────────────
  // A module-scope STRUCT variable's leaf declares must land at module top (a
  // lazy declare would emit them inside the first use's if/uif arm — see the
  // note at the array pre-declare in lower_module), but declare_struct_leaves
  // reads `wire_syms_`, so it cannot run before the classification above.
  // Doing it there locked EVERY struct net into `mut`; a `mut` binds nothing in
  // tolg until its first store, so a reader the dataflow sort placed ahead of
  // the writer resolved to nil and the connection was silently severed
  // (`unresolved ref 'id_vpu_core_ctrl.<leaf>'`, 428 leaves in minion, emitted
  // as `65'sb1????…` in the netlist).
  //
  // Promote to `wire` ONLY where a plain single-driver wire is valid — the SAME
  // test the split above uses to decide it needs no accumulator. The split
  // device is scalar-only (it skips structs at the `ct.isStruct()` guard), so a
  // partially / multiply written struct has no accumulator to fall back on: a
  // conditional write would branch-merge against the wire's own buffer output
  // (a self-loop), and co-writers would silently last-wins. Those keep `mut`.
  // A single WHOLE procedural store that STRICT definite_blocking_writes proves
  // runs on every path is also a valid one-driver wire; allowing it is
  // essential for an always_comb struct value consumed by an earlier instance
  // in a coarse false SCC. Instance-output and single-continuous-assign nets
  // remain valid position-independent wires as before.
  for (const auto& member : scope.members()) {
    if (member.kind != SymbolKind::Variable) {
      continue;
    }
    const auto& vsym = member.as<slang::ast::VariableSymbol>();
    if (reg_syms_.contains(&vsym) || declared_.contains(&vsym)) {
      continue;
    }
    if (!vsym.getType().getCanonicalType().isStruct()) {
      continue;
    }
    if (wire_syms_.contains(&vsym)) {
      auto      wit          = writers_of.find(&vsym);
      const int driver_count = wit != writers_of.end() ? static_cast<int>(wit->second.size()) : 0;
      auto      scit         = store_counts.find(&vsym);
      const int store_count  = scit != store_counts.end() ? scit->second : 0;
      if (driver_count > 1 || store_count > 1 || partial_writes.contains(&vsym)
          || (proc_written.contains(&vsym) && !definite_proc_written.contains(&vsym)) || wire_split_tmp_.contains(&vsym)) {
        wire_syms_.erase(&vsym);  // no scalar split to fall back on: keep `mut`
      }
    }
    declare_value_symbol(vsym, /*force_reg=*/false);
  }

  // ── hoist every WIRE-classified scalar declare to module top ───────────────
  // Same hazard as the regs hoisted in lower_module: the declare is otherwise
  // emitted lazily at first use, and when that use sits in an if/case arm the
  // `wire` declare lands INSIDE the arm — lower_branch then rolls the binding
  // back at arm exit, so a sibling arm's read finds nothing
  // (minion_dcache_cache_op_unit's clear_line). Module-top is where the buffer
  // belongs; wire reads are position-independent by construction, so hoisting
  // cannot change any value. Skips split wires (pass 4's preamble declares
  // those, and it sets `declared_` precisely to suppress this) and anything
  // with its own aggregate machinery.
  for (const auto& member : scope.members()) {
    if (member.kind != SymbolKind::Variable && member.kind != SymbolKind::Net) {
      continue;
    }
    const auto& vsym = member.as<slang::ast::ValueSymbol>();
    if (!wire_syms_.contains(&vsym) || declared_.contains(&vsym) || reg_syms_.contains(&vsym) || wire_split_tmp_.contains(&vsym)) {
      continue;
    }
    declare_value_symbol(vsym, /*force_reg=*/false);
  }

  // ── hoist every remaining WRITTEN local's `mut` declare to module top ──────
  // The last lazy-declare hazard. A plain packed local that is neither a reg,
  // a wire, nor an aggregate has no pre-declare at all: it is declared at its
  // FIRST TOUCH, and that touch can sit inside an if/case arm. cvfpu's
  // fpnew_opgroup_multifmt_slice writes `local_operands[i]` from an unrolled
  // `for` body's `if (i == 2) … else …`, so the declare landed in the THEN arm.
  // Two failures follow: the SIBLING arm's write targets a name that is no
  // longer bound (lower_branch rolls an in-arm declare's binding back at arm
  // exit), and — because `i == 2` is comptime-false on the first iteration —
  // upass DELETES the dead arm together with the declare inside it, so nothing
  // declares the net at ALL ("assignment to undeclared variable
  // 'gen_num_lanes_0_active_lane_local_operands'" when the emitted Pyrope is
  // recompiled). Module top is where a `mut` accumulator belongs, exactly as
  // for the regs/arrays/structs/wires hoisted above: the declare carries only
  // the x-fill poison (`= 0sb?`), which every writer overwrites, so moving it
  // ahead of the drivers cannot change a value.
  //
  // Scalars-only and WRITTEN-only, to keep the blast radius at the class that
  // is actually broken: an aggregate rides its own pre-declare machinery, and a
  // never-written local would gain a poison driver it does not have today.
  // Recurses through generate blocks — the symbol is typically a genblock local
  // (`begin : active_lane`), which `scope.members()` alone never reaches.
  {
    std::function<void(const slang::ast::Scope&)> hoist_muts = [&](const slang::ast::Scope& sc) {
      for (const auto& member : sc.members()) {
        // Mirror pass 1's prefix bookkeeping EXACTLY: lname_of() memoizes the
        // flattened name the FIRST time it is asked, off `genblk_prefix_`, so
        // naming a genblock local from an empty prefix here would rename it
        // (`gen_num_lanes_0_active_lane_local_operands` -> `local_operands`)
        // for the whole module.
        if (member.kind == SymbolKind::GenerateBlock) {
          const auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
          if (gen.isUninstantiated) {
            continue;
          }
          auto saved_prefix = genblk_prefix_;
          if (!gen.name.empty() || gen.getParentScope()->asSymbol().kind != SymbolKind::GenerateBlockArray) {
            genblk_prefix_ = absl::StrCat(genblk_prefix_, gen.getExternalName(), "_");
          }
          hoist_muts(gen);
          genblk_prefix_ = saved_prefix;
          continue;
        }
        if (member.kind == SymbolKind::GenerateBlockArray) {
          const auto& arr          = member.as<slang::ast::GenerateBlockArraySymbol>();
          auto        saved_prefix = genblk_prefix_;
          for (const auto* entry : arr.entries) {
            std::string idx_txt
                = entry->arrayIndex != nullptr ? entry->arrayIndex->toString() : std::to_string(entry->constructIndex);
            genblk_prefix_ = absl::StrCat(saved_prefix, arr.getExternalName(), "_", idx_txt, "_");
            hoist_muts(*entry);
          }
          genblk_prefix_ = saved_prefix;
          continue;
        }
        if (member.kind != SymbolKind::Variable) {
          continue;  // a Net's driver is a ContinuousAssign — never inside an arm
        }
        const auto& vsym = member.as<slang::ast::ValueSymbol>();
        if (declared_.contains(&vsym) || reg_syms_.contains(&vsym) || wire_syms_.contains(&vsym) || wire_split_tmp_.contains(&vsym)
            || input_syms_.contains(&vsym) || output_syms_.contains(&vsym)) {
          continue;
        }
        const auto& ct = vsym.getType().getCanonicalType();
        if (!ct.isIntegral() || !ct.hasFixedRange() || ct.isStruct() || ct.isPackedUnion() || is_scalar_struct_var(vsym)
            || mem_syms_.contains(&vsym) || flat_port_syms_.contains(&vsym)) {
          continue;  // structs / memories / flattened buses have their own pre-declare
        }
        if (!writers_of.contains(&vsym)) {
          continue;  // never driven: leave the (already undriven) read path alone
        }
        declare_value_symbol(vsym, /*force_reg=*/false);
      }
    };
    hoist_muts(scope);
  }
  // ── hoist plain module-level combinational variables ──────────────────────

  // ── a WIRE-classified OUTPUT port needs its buffer too ─────────────────────
  // An output port is declared from io_meta, so `declared_` already holds it
  // and the lazy declare_value_symbol path never fires; the poison loop below
  // then deliberately skips it (a wire is single-driver, a poison would be a
  // second driver). Net effect today: the name binds NOTHING, so a read the
  // dataflow sort placed ahead of the driver resolves to nil — prim_mul_div's
  // `req_ready`, txfma_e2's `exp_res_2f3_f2a_h`, vpu_lane_tima's `tima_out_o`.
  // Emit the same `wire` declare every other cyclic net gets: tolg binds the
  // name to a passthrough buffer, stores route to its din, and build() wires
  // the buffer output to the graph output sink. Deterministic order.
  {
    std::vector<const slang::ast::Symbol*> wouts;
    for (const auto* sym : output_syms_) {
      if (!wire_syms_.contains(sym) || reg_syms_.contains(sym) || !sym_lname_.contains(sym)) {
        continue;
      }
      const auto* vs = sym->as_if<slang::ast::ValueSymbol>();
      if (vs == nullptr || wire_split_tmp_.contains(sym)) {
        continue;
      }
      // Scalars only: an aggregate output rides the bundle/shadow machinery.
      const auto& ct = vs->getType().getCanonicalType();
      if (!ct.isIntegral() || ct.isStruct() || ct.isPackedUnion() || is_scalar_struct_var(*vs) || mem_syms_.contains(vs)) {
        continue;
      }
      const auto& ln = sym_lname_.at(sym);
      if (!ln.empty() && ln.front() == '`') {
        continue;  // escaped (dotted) name — a struct/tuple LEAF, not a scalar
      }
      wouts.push_back(sym);
    }
    std::sort(wouts.begin(), wouts.end(), sym_emit_less);
    for (const auto* sym : wouts) {
      auto ti = tinfo(sym->as<slang::ast::ValueSymbol>().getType());
      set_pending_loc(sym->location);
      builder_.create_declare_stmts(sym_lname_.at(sym),
                                    "wire",
                                    int_max_str(ti.bits, ti.is_signed),
                                    int_min_str(ti.bits, ti.is_signed));
      clear_pending_loc();
    }
  }

  // X-default poison-init for every COMBINATIONAL (non-reg) output: emit
  // `out = 0sb?` / `0ub?…?` before any driver. A legal Verilog output the body
  // never drives defaults to X — this makes that explicit (Pyrope would
  // otherwise reject an undriven output). A real driver supersedes it (the
  // coalescer DSE-drops it, or a conditional drive folds it to the mux ELSE arm
  // = correct X-when-not-selected). This is what lets tolg drop its
  // is_verilog_origin undriven-output leniency and keep the hard error for a
  // genuinely-undriven PYROPE output. Output regs already have a q-pin driver,
  // so they are excluded (a poison-init would double-drive) — and so is a
  // WIRE-classified output (a wire is single-driver; the poison store would be
  // a second driver, and a SPLIT wire's accumulator already carries the poison
  // as its declare init). Deterministic order.
  {
    std::vector<const slang::ast::Symbol*> couts;
    for (const auto* sym : output_syms_) {
      if (reg_syms_.contains(sym) || !output_info_.contains(sym) || !sym_lname_.contains(sym) || wire_syms_.contains(sym)) {
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
    std::sort(couts.begin(), couts.end(), sym_emit_less);
    for (const auto* sym : couts) {
      set_pending_loc(sym->location);
      // `0sb?` is the WIDTH-TAKING wildcard (uPass_runner::resolve_x_fill): the
      // `?` replicates into the destination's DECLARED width — for a port, its
      // io_meta type — and stops there. Same value as a `bits`-long run of `?`,
      // but minion has 512- and 1024-bit combinational outputs and one such
      // literal is a kilobyte of LNAST text. The signed arm was already this
      // form (a signed destination has no bounded-width all-unknown Dlop to
      // narrow to, so the fill deliberately leaves it alone).
      builder_.create_assign_stmts(sym_lname_.at(sym), "0sb?");
      clear_pending_loc();
    }
    // M7: every COMB bundle OUTPUT port drives a local per-field SHADOW
    // accumulator (`<port>__bpo.<field>` mut leaves, poison-initialized like
    // the flat-path outputs); the port leaves themselves get exactly ONE
    // top-level store each — the end-of-module bridge below. This keeps the
    // RECOMPILE of the emitted Pyrope on the safe side of upass.ssa's
    // port-tuple flatten (>=2 top-level stores to an output tuple leaf make
    // the version commit clobber every if-arm store — writeback_unit's
    // l2_req_data_o refuted as constant 0). Reg-driven bundle ports were
    // re-routed at declare_reg (their own single-store bridge), so they are
    // no longer in bundle_port_info_ here.
    std::vector<const slang::ast::Symbol*> bouts;
    for (const auto& [sym, si] : bundle_port_info_) {
      if (!output_syms_.contains(sym) || reg_syms_.contains(sym) || !sym_lname_.contains(sym)) {
        continue;
      }
      bouts.push_back(sym);
    }
    std::sort(bouts.begin(), bouts.end(), sym_emit_less);
    for (const auto* sym : bouts) {
      const auto  fields = bundle_port_info_.at(sym).fields;  // copy (builder can rehash)
      std::string stem   = sym_lname_.at(sym);
      if (!stem.empty() && stem.front() == '`') {
        stem = stem.substr(1, stem.size() - 2);
      }
      std::string shadow = absl::StrCat(stem, "__bpo");
      for (int n = 0; used_names_.contains(shadow); ++n) {
        shadow = absl::StrCat(stem, "__bpo", n);
      }
      used_names_.insert(shadow);
      set_pending_loc(sym->location);
      for (const auto& f : fields) {
        auto leaf = absl::StrCat(shadow, ".", f.name);
        builder_.create_declare_stmts(leaf, "mut", "", "");
        if (f.is_signed) {
          builder_.create_assign_stmts(leaf, "0sb?");
        } else {
          std::string qmarks(static_cast<size_t>(f.bits > 0 ? f.bits : 1), '?');
          builder_.create_assign_stmts(leaf, absl::StrCat("0ub", qmarks));
        }
      }
      clear_pending_loc();
      bundle_out_shadow_.emplace(sym, std::move(shadow));
    }
  }

  // ── pass 4: emit ──────────────────────────────────────────────────────────
  // Cyclic drivers emit last (after the topologically-ordered ones); their nets
  // are declared `wire` (2c-wire), so a forward read binds to the resolved net.
  //
  // Split wires (multiply-written): pre-declare `mut <tmp>` (poison) + `wire
  // <net>` (real width) up front, before any driver — the wire is
  // position-independent so an early read binds to its later bridge driver.
  // wire_split_tmp_ is pointer-keyed; this loop appends IR, so linearize it.
  for (const auto* wsym : emit_ordered(wire_split_tmp_)) {
    const std::string& tmp = wire_split_tmp_.at(wsym);
    const auto*        vs  = wsym->as_if<slang::ast::ValueSymbol>();
    if (vs == nullptr) {
      continue;
    }
    // A FLATTENED-AGGREGATE split: the mut accumulator is the ALREADY-declared
    // flat bus — declare only the reader-side wire, at the bus's flat width.
    if (wire_split_flat_.contains(wsym)) {
      const auto& mi        = mem_info_.at(vs);
      const int   flat_bits = mi.elem_bits * static_cast<int>(mi.size);
      set_pending_loc(vs->location);
      builder_.create_declare_stmts(lname_of(*vs), "wire", int_max_str(flat_bits, false), int_min_str(flat_bits, false));
      clear_pending_loc();
      continue;
    }
    auto ti  = tinfo(vs->getType());
    auto foo = lname_of(*vs);
    set_pending_loc(vs->location);
    // Poison rides the declare's INIT child (`mut tmp = 0ub?…`), NOT a separate
    // store: the accumulator is written far from its declare (a later driver's
    // mux), so a separate poison store would survive the writer's mux-fold and
    // read back as a SECOND `mut tmp = …` (redeclaration). As a declare init it
    // is rendered once by write_declare, which marks the name declared so the
    // accumulating writes reassign without a `mut` prefix.
    // An UNSIGNED accumulator states its width in the DECLARE and poisons with
    // the width-taking wildcard: `mut tmp:uN = 0sb?` is the same value as the
    // width-matched `0ub????…` literal (upass fills the `?` to the declared
    // width) and does not put N `?` on the line — some of these buses are 1024
    // bits wide. A SIGNED one keeps the untyped form: the fill is unsigned-only
    // (Dlop has no bounded-width all-unknown signed value to narrow to).
    if (ti.is_signed) {
      builder_.create_declare_stmts(tmp, "mut", "", "", "0sb?");
    } else {
      builder_.create_declare_stmts(tmp, "mut", int_max_str(ti.bits, false), int_min_str(ti.bits, false), "0sb?");
    }
    builder_.create_declare_stmts(foo, "wire", int_max_str(ti.bits, ti.is_signed), int_min_str(ti.bits, ti.is_signed));
    clear_pending_loc();
    declared_.insert(wsym);  // suppress the lazy wire declare
  }

  auto emit_driver = [&](size_t i) {
    const auto& d            = drivers[i];
    auto        saved_prefix = genblk_prefix_;
    genblk_prefix_           = d.prefix;
    // A split continuous equation reads the RESOLVED wire, not the accumulator
    // that receives its own partial store. Lower its RHS before redirecting the
    // written symbol to `__wtmp`; this makes sibling slice drivers concurrent
    // instead of snapshotting poison in source order.
    std::optional<std::string> resolved_cont_rhs;
    if (d.member != nullptr && d.member->kind == SymbolKind::ContinuousAssign) {
      for (const auto* w : d.writes) {
        if (resolved_cont_selfrefs.contains(w) && d.rhs_reads.contains(w)) {
          const auto& raw = d.member->as<slang::ast::ContinuousAssignSymbol>().getAssignment();
          if (raw.kind == ExpressionKind::Assignment) {
            // Install the continuous-assign context lower_continuous_assign
            // would install: this runs BEFORE it, so the PREVIOUS driver's
            // procedural state (proc_blocking_written_ gates the lazy declare
            // in lower_named_value) would otherwise still be in effect, and
            // the pending loc would be empty so the RHS statements would carry
            // no source mapping.
            proc_kind_ = Proc_kind::none;
            proc_assign_style_.clear();
            proc_blocking_written_.clear();
            current_assign_nonblocking_ = false;
            set_pending_loc(raw.sourceRange);
            resolved_cont_rhs = to_int_value(lower_rvalue(raw.as<slang::ast::AssignmentExpression>().right()));
            clear_pending_loc();
          }
          break;
        }
      }
    }
    // Split-wire redirect: while THIS driver (a writer of the net) emits, its
    // writes AND its own RMW reads resolve to the `mut` accumulator; every other
    // driver keeps reading the resolved wire.
    std::vector<std::pair<const slang::ast::Symbol*, std::string>> restore;
    if (!wire_split_tmp_.empty()) {
      for (const auto* w : d.writes) {
        auto it = wire_split_tmp_.find(w);
        if (it != wire_split_tmp_.end()) {
          restore.emplace_back(w, sym_lname_[w]);
          sym_lname_[w] = it->second;
        }
      }
    }
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
        if (const char* dbg = std::getenv("SLANG_DUMP_NETINIT"); dbg != nullptr && ns.name.find(dbg) != std::string_view::npos) {
          std::fprintf(stderr,
                       "[SLANG_NETINIT] '%s' scalar_struct=%d all_scalar=%d field_read=%d deep_access=%d "
                       "whole_copied=%d deep_written=%d\n",
                       std::string(ns.name).c_str(),
                       is_scalar_struct_var(ns) ? 1 : 0,
                       struct_is_all_scalar(ns) ? 1 : 0,
                       struct_field_read_.contains(&ns) ? 1 : 0,
                       struct_deep_accessed_.contains(&ns) ? 1 : 0,
                       struct_whole_copied_.contains(&ns) ? 1 : 0,
                       struct_deep_written_.contains(&ns) ? 1 : 0);
        }
        if (is_scalar_struct_var(ns)
            && (std::getenv("SLANG_NETINIT_OLDGATE") == nullptr || struct_is_all_scalar(ns) || struct_field_read_.contains(&ns))) {
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
      case SymbolKind::ContinuousAssign:
        lower_continuous_assign(d.member->as<slang::ast::ContinuousAssignSymbol>(),
                                resolved_cont_rhs ? &*resolved_cont_rhs : nullptr);
        break;
      case SymbolKind::ProceduralBlock : lower_process(d.member->as<slang::ast::ProceduralBlockSymbol>()); break;
      case SymbolKind::Instance        : lower_instance(d.member->as<slang::ast::InstanceSymbol>()); break;
      case SymbolKind::UninstantiatedDef:
        lower_unknown_instance(d.member->as<slang::ast::UninstantiatedDefSymbol>(), d.bb_outs);
        break;
      default: break;
    }
    genblk_prefix_ = saved_prefix;
    for (auto& [w, name] : restore) {
      sym_lname_[w] = name;  // → back to the resolved wire for other drivers
    }
  };

  for (size_t i : order) {
    emit_driver(i);
  }
  for (size_t i : cyclic) {
    emit_driver(i);
  }

  // Split-wire bridges: the single driver of each split wire, `<net> =
  // <net>__wtmp`, emitted after every write to the accumulator has landed.
  for (const auto* wsym : emit_ordered(wire_split_tmp_)) {
    const auto* vs = wsym->as_if<slang::ast::ValueSymbol>();
    if (vs == nullptr) {
      continue;
    }
    set_pending_loc(vs->location);
    builder_.create_assign_stmts(lname_of(*vs), wire_split_tmp_.at(wsym));
    clear_pending_loc();
  }

  // M7 bundle-output bridges: `port.field = <shadow>.field`, ONE top-level
  // store per port leaf, after every body driver has written the shadow.
  // Deterministic order (pointer-keyed map iteration varies run-to-run).
  {
    std::vector<const slang::ast::Symbol*> bports;
    for (const auto& [sym, shadow] : bundle_out_shadow_) {
      bports.push_back(sym);
    }
    std::sort(bports.begin(), bports.end(), sym_emit_less);
    for (const auto* sym : bports) {
      auto it = bundle_port_info_.find(sym);
      if (it == bundle_port_info_.end() || !sym_lname_.contains(sym)) {
        continue;
      }
      const auto fields = it->second.fields;  // copy (builder can rehash)
      const auto shadow = bundle_out_shadow_.at(sym);
      const auto base   = sym_lname_.at(sym);
      set_pending_loc(sym->location);
      for (const auto& f : fields) {
        auto v = read_leaf(absl::StrCat(shadow, ".", f.name));
        emit_leaf_store(absl::StrCat(base, ".", f.name), v);
      }
      clear_pending_loc();
    }
  }
}

void Slang_context::lower_continuous_assign(const slang::ast::ContinuousAssignSymbol& ca, const std::string* precomputed_rhs) {
  proc_kind_ = Proc_kind::none;
  proc_assign_style_.clear();
  proc_blocking_written_.clear();

  if (ca.getDelay() != nullptr) {
    emit_warning(slang::SourceRange(ca.location, ca.location),
                 "delay-ignored",
                 "unsupported",
                 "assignment delay is ignored (synthesis semantics)");
  }

  const auto& as = ca.getAssignment();
  if (as.kind != ExpressionKind::Assignment) {
    emit_unsupported(ca.location, "unsupported-continuous-assign", "unsupported continuous assignment shape");
    return;
  }
  set_pending_loc(as.sourceRange);
  const auto& assign = as.as<slang::ast::AssignmentExpression>();
  if (precomputed_rhs != nullptr) {
    current_assign_nonblocking_ = false;
    assign_to(assign.left(), *precomputed_rhs);
  } else {
    lower_assign(assign);
  }
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
      if (tok == "rst" || tok == "reset" || tok == "rstn" || tok == "resetn" || tok == "arst" || tok == "areset" || tok == "nrst"
          || tok == "nreset" || tok == "por") {
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
      emit_warning(slang::SourceRange(pbs.location, pbs.location),
                   "initial-ignored",
                   "unsupported",
                   "initial block is ignored (synthesis semantics)");
      return;
    case ProceduralBlockKind::Final     : return;
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

  const auto&                                       stmt           = pbs.getBody();
  // A standalone `assert/assume/cover property(...)` is modeled by slang as an
  // implicit Always procedural block whose body is the assertion (possibly
  // wrapped in a Block/List), NOT a Timed event-control. Assertions are not
  // synthesized, so ignore such bodies (mirrors lower_statement in slang_stmt.cpp).
  std::function<bool(const slang::ast::Statement&)> assertion_only = [&](const slang::ast::Statement& s) -> bool {
    switch (s.kind) {
      case StatementKind::Empty:
      case StatementKind::ImmediateAssertion:
      case StatementKind::ConcurrentAssertion: return true;
      case StatementKind::Block              : return assertion_only(s.as<slang::ast::BlockStatement>().body);
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
    emit_unsupported(stmt.sourceRange,
                     "unsupported-always",
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
      case TimingControlKind::SignalEvent  : {
        const auto& se = tc.as<slang::ast::SignalEventControl>();
        if (se.iffCondition != nullptr) {
          emit_unsupported(tc.sourceRange, "unsupported-iff", "iff event qualifiers are not supported");
          bad = true;
          return;
        }
        switch (se.edge) {
          case slang::ast::EdgeKind::None   : implicit = true; break;  // @(a or b) sensitivity-list style
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
    emit_unsupported(timed.timing.sourceRange,
                     "edge-implicit-mixing",
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
  const slang::ast::Statement*              body       = &timed.stmt;
  bool                                      peeled_any = false;  // a rung already extracted?
  std::vector<std::string>                  inactive_async_guards;

  // A constant bit-select is a plain one-bit event signal too. cgen uses this
  // form when a boolean reset cone retains harmless width headroom, emitting
  // the same `net[0]` in the event control and guard. Keep the selected bit in
  // the identity so `posedge bus[0]` cannot accidentally match `if (bus[1])`.
  struct Plain_event_signal {
    const slang::ast::ValueSymbol* sym = nullptr;
    std::optional<int64_t>         bit;
  };
  auto plain_event_signal = [&](const slang::ast::Expression& raw) -> std::optional<Plain_event_signal> {
    const slang::ast::Expression* e = &raw;
    while (e->kind == ExpressionKind::Conversion) {
      e = &e->as<slang::ast::ConversionExpression>().operand();
    }
    if (e->kind == ExpressionKind::NamedValue) {
      return Plain_event_signal{&e->as<slang::ast::NamedValueExpression>().symbol, std::nullopt};
    }
    if (e->kind != ExpressionKind::ElementSelect) {
      return std::nullopt;
    }
    const auto& sel  = e->as<slang::ast::ElementSelectExpression>();
    const auto* base = &sel.value();
    while (base->kind == ExpressionKind::Conversion) {
      base = &base->as<slang::ast::ConversionExpression>().operand();
    }
    auto bit = try_eval_int(sel.selector());
    if (!bit || base->kind != ExpressionKind::NamedValue) {
      return std::nullopt;
    }
    return Plain_event_signal{&base->as<slang::ast::NamedValueExpression>().symbol, *bit};
  };

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
      return rsc != nullptr && (&rsc->asSymbol() == body_ || rsc->asSymbol().kind == slang::ast::SymbolKind::GenerateBlock);
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
      const slang::ast::Statement*              cond = nullptr;
      bool                                      ok   = true;
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
      emit_unsupported(body->sourceRange,
                       "unsupported-async-pattern",
                       "expected `if (rst) ... else ...` rungs for the extra edge triggers",
                       "use a synchronous reset, or --reader yosys-slang");
      return;
    }
    const auto& cond_stmt = body->as<slang::ast::ConditionalStatement>();
    if (cond_stmt.conditions.size() != 1 || cond_stmt.conditions[0].pattern != nullptr || cond_stmt.ifFalse == nullptr) {
      if (demote_reset_edges()) {
        break;
      }
      emit_unsupported(body->sourceRange,
                       "unsupported-async-pattern",
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
    auto cond_signal = plain_event_signal(*cond);
    if (!cond_signal) {
      if (demote_reset_edges()) {
        break;
      }
      emit_unsupported(cond->sourceRange,
                       "unsupported-async-pattern",
                       "the async-reset condition must be a plain signal or constant bit-select");
      return;
    }
    const auto* rst_sym = cond_signal->sym;

    // Match the condition signal to one of the extra edge triggers.
    size_t match = edges.size();
    for (size_t i = 0; i < edges.size(); ++i) {
      auto edge_signal = plain_event_signal(edges[i]->expr);
      if (edge_signal && edge_signal->sym == rst_sym && edge_signal->bit == cond_signal->bit) {
        match = i;
        break;
      }
    }
    if (match == edges.size()) {
      if (demote_reset_edges()) {
        break;
      }
      emit_unsupported(cond->sourceRange,
                       "unsupported-async-pattern",
                       "the async-reset condition does not name one of the edge triggers");
      return;
    }
    const bool edge_pos = edges[match]->edge == slang::ast::EdgeKind::PosEdge;
    if (edge_pos != polarity) {
      emit_warning(cond->sourceRange,
                   "async-reset-polarity",
                   "time",
                   "the async-reset guard polarity does not match its edge trigger");
    }
    // A reset synchronizer drives the flop from a DERIVED module-level signal
    // (rst_int_ni = scanmode ? scan_reset_n : rst_ni), not a module input.
    // Accept an input OR a module-level net/var (tolg resolves its driver and
    // wires it to reset_pin). A local/block-scoped signal still has no stable
    // cut driver -> reject.
    if (!input_syms_.contains(rst_sym)) {
      const auto* rsc = rst_sym->getParentScope();
      const bool  module_level
          = rsc != nullptr && (&rsc->asSymbol() == body_ || rsc->asSymbol().kind == slang::ast::SymbolKind::GenerateBlock);
      if (!module_level) {
        if (demote_reset_edges()) {
          break;
        }
        emit_unsupported(cond->sourceRange,
                         "unsupported-async-pattern",
                         "the async reset must be a module input or a module-level signal");
        return;
      }
      if (!declared_.contains(rst_sym)) {
        declare_value_symbol(*rst_sym, /*force_reg=*/false);
      }
    }

    std::string reset_ref_name = lname_of(*rst_sym);
    if (cond_signal->bit) {
      // A flop reset_pin names one scalar graph signal; pointing it at the
      // selected vector's base silently changes `bus[k]` into "bus is nonzero".
      // Materialize the exact selected bit as a one-bit wire and use that wire
      // for the reset attribute. This also gives cgen a stable scalar name on
      // the next round trip.
      std::string stem = absl::StrCat(reset_ref_name, "__async_reset_bit", *cond_signal->bit);
      reset_ref_name   = stem;
      for (int n = 0; used_names_.contains(reset_ref_name); ++n) {
        reset_ref_name = absl::StrCat(stem, "_", n);
      }
      used_names_.insert(reset_ref_name);
      set_pending_loc(cond->sourceRange.start());
      builder_.create_declare_stmts(reset_ref_name, "wire", int_max_str(1, false), int_min_str(1, false));
      builder_.create_assign_stmts(reset_ref_name, lower_rvalue(*cond));
      clear_pending_loc();
    }

    // The then-arm must be const nonblocking stores to regs: those become
    // the reset values. Validate-and-collect first, emit after — a partial
    // emit would leave stray reset attrs behind when a later statement fails
    // the walk and the demote fallback lowers the arm synchronously instead.
    std::vector<std::pair<const slang::ast::ValueSymbol*, std::string>> reset_stores;
    // PARTIAL (bit-range) reset writes: `if (!rst_b) begin q[9:1] <= 0; q[0] <= 1; end`
    // is one constant reset value spelled across several slices. Requiring a
    // whole-reg NamedValue write rejected it, and the whole always block then
    // demoted to a SYNCHRONOUS reset -- a real behaviour change (the reset no
    // longer takes effect off the clock edge), which LEC refutes. Accumulate the
    // slices per reg and fold them into one `initial=` value once every bit of
    // the reg is covered; anything short of full coverage still demotes, because
    // the uncovered bits would have to HOLD, which a reset value cannot express.
    struct Partial {
      uint64_t value = 0;
      uint64_t mask  = 0;  // bits written so far
      uint64_t full  = 0;  // all bits of the reg
    };
    std::vector<std::pair<const slang::ast::ValueSymbol*, Partial>> partials;
    auto partial_of = [&](const slang::ast::ValueSymbol* sym) -> Partial* {
      for (auto& [s, p] : partials) {
        if (s == sym) {
          return &p;
        }
      }
      partials.emplace_back(sym, Partial{});
      return &partials.back().second;
    };
    std::function<bool(const slang::ast::Statement&)> harvest = [&](const slang::ast::Statement& s) -> bool {
      switch (s.kind) {
        case StatementKind::Empty: return true;
        case StatementKind::Block: return harvest(s.as<slang::ast::BlockStatement>().body);
        case StatementKind::List : {
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
          if (sym == nullptr || !reg_syms_.contains(sym)) {
            return false;
          }
          // NOTE (provenance, deferred): a bare `q <= PKG_PARAM` reset load
          // still FOLDS here — carrying the name through the `initial` attr as
          // a ref mis-resolves on recompile (LEC-refuted), so it needs the
          // attr-resolution work first (see provenance.md M6).
          auto cv = try_eval_const_net(as.right());
          if (!cv || !cv->isInteger()) {
            return false;
          }
          if (as.left().kind == ExpressionKind::NamedValue) {
            reset_stores.emplace_back(sym, const_text(cv->integer()));
            return true;
          }
          // A constant SLICE of the reg: use the normal packed-lvalue resolver
          // so multi-dimensional packed arrays get the same flattened offset
          // as their ordinary writes (`q[1]` in `[1:0][3:0] q` starts at bit
          // four, not bit one).
          const auto& lhs_e = as.left();
          if (lhs_e.kind != ExpressionKind::ElementSelect && lhs_e.kind != ExpressionKind::RangeSelect) {
            return false;
          }
          Packed_lv lv;
          if (!resolve_packed_lvalue(lhs_e, lv) || lv.base != sym || !lv.dyn_off.empty()) {
            return false;
          }
          const uint64_t reg_bits = sym->getType().getBitWidth();
          const int64_t  slice_w  = lv.width;
          const int64_t  lo       = lv.const_off;
          if (reg_bits == 0 || reg_bits > 63 || slice_w <= 0 || lo < 0 || static_cast<uint64_t>(lo + slice_w) > reg_bits) {
            return false;  // >63 bits does not fit the uint64 accumulator
          }
          const uint64_t slice_mask = slice_w >= 64 ? ~uint64_t{0} : ((uint64_t{1} << slice_w) - 1);
          const uint64_t slice_val  = cv->integer().as<uint64_t>().value_or(0) & slice_mask;
          auto*          p          = partial_of(sym);
          p->full                   = (uint64_t{1} << reg_bits) - 1;
          const uint64_t placed     = slice_mask << static_cast<uint64_t>(lo);
          if ((p->mask & placed) != 0) {
            return false;  // overlapping writes: last-wins ordering is not modelled here
          }
          p->mask  |= placed;
          p->value |= slice_val << static_cast<uint64_t>(lo);
          return true;
        }
        default: return false;
      }
    };
    bool harvested = harvest(cond_stmt.ifTrue);
    // A symbol that ALSO has a continuous-assign driver is only PARTLY a
    // register, so `full` (its whole declared width) is a denominator the reset
    // slices can never reach — queueing it would only guarantee a spurious
    // `unsupported-partial-async-reset` once the module has lowered. cvfpu's
    // pipelines are exactly this shape (`assign q[0] = in;` + `FFL(q[i+1],
    // q[i], …)`, 77 sites in CVA6). Take the pre-existing demote-to-synchronous
    // path for those instead, which is what this reader did before the slice
    // accumulator existed.
    //
    // Checked BEFORE the queueing loop below, not inside it: bailing out
    // mid-loop would leave the SIBLING symbols already queued in
    // pending_async_resets_ while this process is demoted to a synchronous
    // reset, so finalize_pending_async_resets would either complete their
    // coverage from stale slices (an async reset attr on a register whose reset
    // was demoted) or report a partial-coverage error this process no longer
    // owns.
    if (harvested) {
      for (const auto& [sym, p] : partials) {
        if (p.mask != p.full && cont_assign_syms_.contains(sym)) {
          harvested = false;
          break;
        }
      }
    }
    if (harvested) {
      // A single process may cover the whole register, or generated sibling
      // processes may cover disjoint slices. Queue the latter until the entire
      // module has lowered; finalize_pending_async_resets requires exact whole-
      // register coverage under one reset control.
      for (const auto& [sym, p] : partials) {
        if (p.mask == p.full) {
          reset_stores.emplace_back(sym, std::to_string(p.value));
          continue;
        }
        auto [it, inserted] = pending_async_resets_.try_emplace(sym);
        auto& pending       = it->second;
        if (inserted) {
          pending.full      = p.full;
          pending.reset_ref = reset_ref_name;
          pending.edge_pos  = edge_pos;
          pending.loc       = cond_stmt.ifTrue.sourceRange.start();
        }
        if (pending.full != p.full || pending.reset_ref != reset_ref_name || pending.edge_pos != edge_pos
            || (pending.mask & p.mask) != 0) {
          pending.invalid = true;
        } else {
          pending.mask  |= p.mask;
          pending.value |= p.value;
        }
      }
    }
    if (!harvested) {
      if (demote_reset_edges()) {
        break;
      }
      emit_unsupported(cond_stmt.ifTrue.sourceRange,
                       "unsupported-async-load",
                       "the async-reset arm must contain only constant non-blocking writes to state regs",
                       "non-constant async loads have no flop lowering; use --reader yosys-slang");
      return;
    }
    for (const auto& [sym, init_text] : reset_stores) {
      emit_reg_reset_attrs(*sym, init_text, reset_ref_name, edge_pos);
    }

    // Peeling the reset arm into per-register attributes must not discard the
    // process-level hold semantics for state that is intentionally NOT reset:
    //
    //   always_ff @(posedge clk or negedge rst_n)
    //     if (!rst_n) ptr <= 0; else if (push) mem[ptr] <= data;
    //
    // `mem` has no reset value, but it still cannot write while reset is held.
    // Keep every peeled reset's inactive condition around the residual body.
    // Reset-bearing flops also receive this redundant data-path gate; their
    // async reset attribute remains authoritative while reset is active.
    auto inactive_guard = booleanize(reset_ref_name);
    if (polarity) {
      inactive_guard = mark_bool(builder_.create_log_not_stmts(inactive_guard));
    }
    inactive_async_guards.push_back(std::move(inactive_guard));

    edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(match));
    body       = cond_stmt.ifFalse;
    peeled_any = true;  // a demote after this point would drop this reset — gate (1)
  }

  lower_ff_process(*edges[0], *body, prologue, inactive_async_guards);
}

void Slang_context::emit_reg_reset_attrs(const slang::ast::ValueSymbol& sym, std::string_view initial, std::string_view reset_ref,
                                         bool edge_pos) {
  declare_reg(sym);  // ensure declared (hoisting normally did)
  auto name = lname_of(sym);
  struct Reset_target {
    std::string name;
    std::string initial;
  };
  std::vector<Reset_target> targets;
  if (auto sit = struct_var_info_.find(&sym); sit != struct_var_info_.end() && reg_syms_.contains(&sym)) {
    // The source reset value is the packed aggregate. Slice it by each leaf's
    // recorded packed offset so every expanded flop gets exactly its bits.
    auto packed = Dlop::from_pyrope(initial);
    if (!packed || packed->is_invalid()) {
      emit_unsupported(sym.location, "unsupported-aggregate-reset", "could not split the packed-aggregate reset value");
      return;
    }
    for (const auto& f : sit->second.fields) {
      auto mask = Dlop::get_mask_value(static_cast<int>(f.off) + f.bits - 1, static_cast<int>(f.off));
      auto lane = packed->get_mask_op(*mask);
      targets.push_back({absl::StrCat(name, ".", f.name), std::string(lane->to_pyrope())});
    }
  } else {
    targets.push_back({name, std::string(initial)});
  }

  auto& ln        = *builder_.lnast;
  auto  emit_attr = [&](std::string_view target, std::string_view key, std::string_view val, bool val_is_ref) {
    auto idx = builder_.add_child(Lnast_ntype::create_attr_set());
    ln.add_child(idx, Lnast_node::create_ref(target));
    ln.add_child(idx, Lnast_node::create_const(key));
    if (val_is_ref) {
      ln.add_child(idx, Lnast_node::create_ref(val));
    } else {
      ln.add_child(idx, Lnast_node::create_const(val));
    }
  };
  // Native packed-memory initialization can already carry a per-entry reset
  // pattern. Do not overwrite it with the scalar packed reset value, which a
  // memory declaration would broadcast to every entry; the reset wiring still
  // belongs on the aggregate.
  const bool lanes_carry_reset = array_reset_lanes_.contains(&sym) && packed_mem_regs_.contains(&sym);
  for (const auto& target : targets) {
    if (!lanes_carry_reset) {
      emit_attr(target.name, "initial", target.initial, false);
    }
    emit_attr(target.name, "reset_pin", reset_ref, true);
    emit_attr(target.name, "sync", "false", false);
    if (!edge_pos) {
      emit_attr(target.name, "negreset", "true", false);
    }
  }
}

void Slang_context::finalize_pending_async_resets() {
  for (const auto* sym : emit_ordered(pending_async_resets_)) {
    const auto& pending = pending_async_resets_.at(sym);
    if (pending.invalid || pending.mask != pending.full) {
      emit_unsupported(pending.loc,
                       "unsupported-partial-async-reset",
                       std::string("asynchronous reset slices of '") + std::string(sym->name)
                           + "' must cover the whole packed register exactly once and use one reset control",
                       "combine the slices under one reset, or use --reader yosys-slang");
      continue;
    }
    emit_reg_reset_attrs(*sym, std::to_string(pending.value), pending.reset_ref, pending.edge_pos);
  }
}

void Slang_context::lower_comb_process(const slang::ast::Statement& body) {
  proc_kind_ = Proc_kind::comb;
  lower_statement(body);
  proc_kind_ = Proc_kind::none;
}

void Slang_context::lower_ff_process(const slang::ast::SignalEventControl& clock, const slang::ast::Statement& body,
                                     std::vector<const slang::ast::Statement*>& prologue,
                                     const std::vector<std::string>&            inactive_async_guards) {
  proc_kind_ = Proc_kind::seq;

  // Identify the clock signal; tolg reuses a declared 1-bit INPUT named
  // `clk`/`clock` implicitly. A LOCAL net with either spelling is still an
  // explicit clock: omitting its clock_pin attr makes tolg mint a new module
  // input named `clock` and silently disconnects the local driver.
  // Other signals/edges ride per-reg attrs after the body (clock_pin / posclk),
  // keyed on the regs this process writes.
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
  const bool implicit_clk = !negedge && input_syms_.contains(clk_sym) && (clk_sym->name == "clk" || clk_sym->name == "clock");

  Write_collector wc;
  body.visit(wc);

  // A blocking-written variable of an edge process that other code reads has
  // flop semantics this reader does not model yet.
  for (const auto* sym : wc.blocking) {
    if (output_syms_.contains(sym)) {
      emit_unsupported(sym->location,
                       "blocking-ff-output",
                       std::string("output '") + std::string(sym->name)
                           + "' is blocking-assigned in an edge-sensitive process; --reader slang only supports `<=` for state",
                       "use a non-blocking assignment");
      continue;
    }
    // An unpacked ARRAY blocking-written in an edge process is the same gap,
    // but it is NOT an output so the check above never saw it — and unlike a
    // scalar process-local temp, a module-scope array is PERSISTENT state.
    // collect_state_vars only admits NONBLOCKING-written symbols to reg_syms_
    // (see its header comment), so such an array is declared `mut`: a
    // combinational, re-zeroed-every-cycle array. That silently drops the
    // storage — measured, `always_ff begin if (we) mem[wa] = wd; q <= mem[ra];
    // end` lowered to `q = (we && wa==ra) ? wd : 0` and lgcheck REFUTED it
    // against the source. Refuse instead of miscompiling (the same fail-stop
    // stance upass.tolg already takes when such an array is read before the
    // write). yosys handles this shape by demoting the memory to per-entry
    // registers (mem2reg); doing the same here is the real fix.
    // The general case of the SAME gap: a blocking-written var of this edge
    // process that something OUTSIDE the process reads is persistent flop
    // state, not a process-local temp (collect_blocking_ff_state decides).
    // Without this it was declared `mut` and the register vanished outright.
    if (blocking_ff_state_.contains(sym)) {
      emit_unsupported(sym->location, "blocking-ff-state",
                       std::string("variable '") + std::string(sym->name)
                           + "' is blocking-assigned in an edge-sensitive process and read outside it; --reader slang only "
                             "supports `<=` for state, and would otherwise lower it as stateless combinational logic",
                       "use a non-blocking assignment (`<=`), or read it with --reader yosys-verilog");
      continue;
    }
    const auto& sct = sym->getType().getCanonicalType();
    if (sct.isUnpackedArray()) {
      emit_unsupported(sym->location, "blocking-ff-array",
                       std::string("array '") + std::string(sym->name)
                           + "' is blocking-assigned in an edge-sensitive process; --reader slang only supports `<=` for "
                             "array state, and would otherwise lower it as a stateless combinational array",
                       "use a non-blocking assignment (`<=`) for the array write, or read it with --reader yosys-verilog");
    }
  }

  for (const auto* stmt : prologue) {
    lower_statement(*stmt);
  }

  // Preserve the enclosing async-reset process arm for registers which do not
  // themselves carry a reset value. Each nested condition is one peeled rung;
  // the clocked body runs only when all asynchronous resets are inactive.
  for (const auto& guard : inactive_async_guards) {
    auto if_nid = builder_.create_if_stmt(false);
    builder_.add_if_cond(if_nid, guard);
    builder_.push_stmts(builder_.add_if_stmts(if_nid));
  }
  lower_statement(body);
  for (size_t i = 0; i < inactive_async_guards.size(); ++i) {
    builder_.pop_stmts();
  }

  if (!implicit_clk) {
    auto& ln = *builder_.lnast;
    // Emit the clock_pin / posclk attr_set nodes in a stable source order:
    // wc.nonblocking is a pointer-keyed flat_hash_set with run-to-run-varying
    // iteration order, and this loop appends IR.
    for (const auto* sym : emit_ordered(wc.nonblocking)) {
      if (!reg_syms_.contains(sym)) {
        continue;
      }
      auto                     name = lname_of(*sym);
      // A TUPLE memory is split by upass.detuple into per-field memories
      // (`mem.field:[N]`), and detuple does not split attr_set — an attr on
      // the BASE name is silently dropped, leaving the per-field memories on
      // the implicit `clock` input (intpipe_csr_msgs' msg_port_conf.* ran on
      // a phantom auto-created `clock` instead of clk_wr_i, on BOTH flows).
      // Emit the clock attrs per FIELD, the same routing as the fwd=0 attr.
      std::vector<std::string> targets;
      if (auto mit = mem_info_.find(sym); mit != mem_info_.end() && mit->second.is_tuple) {
        for (const auto& f : mit->second.fields) {
          targets.push_back(absl::StrCat(name, ".", f.name));
        }
      } else if (auto sit = struct_var_info_.find(sym); sit != struct_var_info_.end() && reg_syms_.contains(sym)) {
        for (const auto& f : sit->second.fields) {
          targets.push_back(absl::StrCat(name, ".", f.name));
        }
      } else {
        targets.push_back(name);
      }
      for (const auto& tgt : targets) {
        auto idx = builder_.add_child(Lnast_ntype::create_attr_set());
        ln.add_child(idx, Lnast_node::create_ref(tgt));
        ln.add_child(idx, Lnast_node::create_const("clock_pin"));
        ln.add_child(idx, Lnast_node::create_ref(lname_of(*clk_sym)));
        if (negedge) {
          auto neg_idx = builder_.add_child(Lnast_ntype::create_attr_set());
          ln.add_child(neg_idx, Lnast_node::create_ref(tgt));
          ln.add_child(neg_idx, Lnast_node::create_const("posclk"));
          ln.add_child(neg_idx, Lnast_node::create_const("false"));
        }
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
    bool                          bundle = false;  // child port is a tuple bundle (M7)
  };
  std::vector<std::pair<std::string, std::string>> in_args;  // (port, value)
  std::vector<Out_conn>                            outs;
  size_t                                           n_outputs_total = 0;
  bool                                             any_bundle_out  = false;

  // Route bits between two leaf tilings of the SAME packed layout WITHOUT a
  // whole-struct reassembly. The two sides can differ in GRAIN — a bundle
  // PORT flattens recursively (`ctrl.req.thread_id`) while a leaf-split
  // struct VAR splits at top level (`ctrl.req` is one leaf) — but both tile
  // the same bit space, so every target field is either tiled exactly by
  // CONTAINED source leaves or is a slice of ONE CONTAINING source leaf.
  // Plans first, emits only on a fully routable mapping (returns false
  // otherwise, with nothing emitted — callers fall back to the flat path).
  // The exact-cover same-sign case passes the value through untouched: the
  // common leaf-to-leaf connect emits ZERO shift/or/mask glue, which is what
  // keeps disjoint fields on separate nets end-to-end (a packed reassembly
  // between two instances unions every field's cone into one node — the
  // false same-cycle ring that refused lhdsuite minion's core<->dcache and
  // core<->vpu control buses).
  auto map_leaves = [&](const std::vector<Struct_info::Field>&                                    tgt,
                        const std::vector<Struct_info::Field>&                                    src,
                        const std::function<std::string(const Struct_info::Field&)>&              read_src,
                        const std::function<void(const Struct_info::Field&, const std::string&)>& emit_tgt) -> bool {
    std::vector<std::vector<const Struct_info::Field*>> tiled(tgt.size());
    std::vector<const Struct_info::Field*>              container(tgt.size(), nullptr);
    for (size_t i = 0; i < tgt.size(); ++i) {
      const auto& tf  = tgt[i];
      int64_t     sum = 0;
      for (const auto& sf : src) {
        if (sf.off >= tf.off && sf.off + sf.bits <= tf.off + tf.bits) {
          tiled[i].push_back(&sf);
          sum += sf.bits;
        }
      }
      if (sum == tf.bits && !tiled[i].empty()) {
        continue;
      }
      tiled[i].clear();
      for (const auto& sf : src) {
        if (tf.off >= sf.off && tf.off + tf.bits <= sf.off + sf.bits) {
          container[i] = &sf;
          break;
        }
      }
      if (container[i] == nullptr) {
        return false;  // grains cross a boundary: not routable leaf-wise
      }
    }
    for (size_t i = 0; i < tgt.size(); ++i) {
      const auto& tf = tgt[i];
      if (tiled[i].size() == 1 && tiled[i][0]->off == tf.off && tiled[i][0]->bits == tf.bits
          && tiled[i][0]->is_signed == tf.is_signed) {
        emit_tgt(tf, read_src(*tiled[i][0]));  // pure alias — no glue
        continue;
      }
      std::string v;
      if (!tiled[i].empty()) {
        for (const auto* sf : tiled[i]) {
          auto sv = to_pattern(to_int_value(read_src(*sf)), sf->bits, sf->is_signed);
          if (sf->off != tf.off) {
            sv = builder_.create_shl_stmts(sv, std::to_string(sf->off - tf.off));
          }
          v = v.empty() ? sv : builder_.create_bit_or_stmts({v, sv});
        }
      } else {
        auto sv = to_pattern(to_int_value(read_src(*container[i])), container[i]->bits, container[i]->is_signed);
        v       = extract_field(sv, tf.off - container[i]->off, tf.bits);
      }
      if (tf.is_signed) {
        v = builder_.create_sext_stmts(v, std::to_string(tf.bits - 1));
      }
      emit_tgt(tf, v);
    }
    return true;
  };

  // M7: an input connection to a child BUNDLE port passes ONE NAMED ACTUAL PER
  // LEAF — `store(ref "<port>.<leaf>", v)` on the func_call — exactly mirroring
  // the bundle OUTPUT side below, which already binds each leaf by its dotted
  // name (`tuple_get(t, result, "<port>.<leaf>")`). tolg binds a named actual by
  // exact GraphIO input name, so this works at ANY nesting depth.
  //
  // It used to pass a single TUPLE literal instead — tuple_add(tmp, store(field,
  // v)...) plus `store(port, ref tmp)` — and uPass_runner then had to re-expand
  // that tuple onto the child's flat leaf params. That expansion is ONE LEVEL
  // only: once a nested struct flattens to dotted leaves (`din.req.bid`), the
  // tuple's dotted keys become a NESTED bundle, `try_tuple_shape` reports the
  // top level (`req`), `<prefix>.req` is not a leaf param, the expansion fails,
  // the caller falls back to passing the struct WHOLE, and every instantiation
  // dies with `fcall-unknown-arg`. Emitting the leaves directly sidesteps the
  // re-expansion entirely and keeps input/output symmetric.
  //
  // expr == nullptr is the unconnected-input x per field.
  auto build_bundle_actual = [&](const slang::ast::PortSymbol& bport, const slang::ast::Expression* aexpr) -> void {
    auto pfields = struct_port_fields(bport.getType());
    bool done    = false;
    if (aexpr == nullptr) {  // unconnected bundle input: every field reads x
      for (const auto& f : pfields) {
        in_args.emplace_back(
            absl::StrCat(bport.name, ".", f.name),
            f.is_signed ? std::string("0sb?") : absl::StrCat("0ub", std::string(static_cast<size_t>(f.bits), '?')));
      }
      done = true;
    } else {
      const slang::ast::Expression* pe = aexpr;
      while (pe->kind == ExpressionKind::Conversion) {
        pe = &pe->as<slang::ast::ConversionExpression>().operand();
      }
      // A peeled Conversion may change the flat WIDTH (packed casts are
      // bit-pattern-preserving, so equal width means offsets line up); the
      // leaf routing below is offset-based, so gate on width equality.
      if (pe->kind == ExpressionKind::NamedValue && flat_or_tinfo(*pe->type).bits == flat_or_tinfo(bport.getType()).bits) {
        const auto& asym     = pe->as<slang::ast::NamedValueExpression>().symbol;
        auto        emit_arg = [&](const Struct_info::Field& tf, const std::string& v) {
          in_args.emplace_back(absl::StrCat(bport.name, ".", tf.name), v);
        };
        if (const auto* bsi = bundle_port_of(asym)) {  // (b) the parent's OWN bundle port: per-leaf reads
          const auto afields = bsi->fields;            // copy: builder calls can rehash the map
          auto       aname   = bundle_port_body_base(asym);
          done               = map_leaves(
              pfields,
              afields,
              [&](const Struct_info::Field& sf) { return read_leaf(absl::StrCat(aname, ".", sf.name)); },
              emit_arg);
        } else if (is_scalar_struct_var(asym)) {  // (a) a local bundle struct var: per-leaf reads
          if (!declared_.contains(&asym)) {
            declare_value_symbol(asym, /*force_reg=*/false);
          }
          if (auto it = struct_var_info_.find(&asym); it != struct_var_info_.end()) {
            const bool a_is_tuple = it->second.is_tuple;
            const auto afields    = it->second.fields;  // copy: builder calls can rehash the map
            auto       aname      = lname_of(asym);
            done                  = map_leaves(
                pfields,
                afields,
                [&](const Struct_info::Field& sf) {
                  return a_is_tuple ? read_struct_field_get(aname, sf.name) : read_leaf(absl::StrCat(aname, ".", sf.name));
                },
                emit_arg);
          }
        }
      }
    }
    if (!done) {  // (c) generic: lower the actual flat, slice per field (always correct)
      auto v  = to_int_value(lower_rvalue(*aexpr));
      auto pi = flat_or_tinfo(bport.getType());
      auto ei = flat_or_tinfo(*aexpr->type);
      v       = materialize_conversion(v, ei.bits, ei.is_signed, pi.bits, pi.is_signed, value_width(*aexpr));
      auto p  = to_pattern(v, pi.bits, pi.is_signed);
      for (const auto& f : pfields) {
        auto fv = extract_field(p, f.off, f.bits);
        if (f.is_signed) {
          fv = builder_.create_sext_stmts(fv, std::to_string(f.bits - 1));
        }
        in_args.emplace_back(absl::StrCat(bport.name, ".", f.name), fv);
      }
    }
  };

  for (const auto* conn : inst.getPortConnections()) {
    if (conn->port.kind != SymbolKind::Port) {
      emit_unsupported(inst.location,
                       "unsupported-port-conn",
                       std::string("instance '") + std::string(inst.name) + "' connects an unsupported port kind");
      return;
    }
    const auto& port   = conn->port.as<slang::ast::PortSymbol>();
    const auto* expr   = conn->getExpression();
    const bool  is_out = port.direction == slang::ast::ArgumentDirection::Out;
    if (port.direction == slang::ast::ArgumentDirection::InOut || port.direction == slang::ast::ArgumentDirection::Ref) {
      emit_unsupported(
          inst.location,
          "unsupported-inout-port",
          std::string("instance '") + std::string(inst.name) + "' connects inout port '" + std::string(port.name) + "'");
      return;
    }
    // M7: the SAME type-only rule the child def used — parents and children
    // stay consistent because neither consults body uses.
    const bool bundle = bundle_port_qualifies(port, callee);
    if (is_out) {
      ++n_outputs_total;
      any_bundle_out |= bundle;
    }

    if (expr == nullptr) {  // unconnected
      if (!is_out) {
        if (bundle) {
          set_pending_loc(inst.location);
          build_bundle_actual(port, nullptr);  // appends one named arg per leaf
          clear_pending_loc();
        } else {
          auto        ti = flat_or_tinfo(port.getType());
          // unconnected input reads x
          std::string qmarks(static_cast<size_t>(ti.bits), '?');
          // A Verilog escaped identifier may contain dots (`\\io_in.field `).
          // Keep that as ONE literal named argument, matching the backticked
          // formal emitted above; an unquoted dotted ref is parsed by upass as
          // a bundle path and fails with fcall-unknown-arg on cgen's own output.
          in_args.emplace_back(ref_name_of_raw(port.name), absl::StrCat("0ub", qmarks));
        }
      }
      continue;
    }

    if (is_out) {
      // slang wraps output connections as `<expr> = EmptyArgument`.
      if (const auto* assign = expr->as_if<slang::ast::AssignmentExpression>()) {
        expr = &assign->left();
      }
      outs.push_back({&port, expr, bundle});
    } else if (bundle) {
      set_pending_loc(expr->sourceRange);
      build_bundle_actual(port, expr);  // appends one named arg per leaf
      clear_pending_loc();
    } else {
      set_pending_loc(expr->sourceRange);
      auto v  = to_int_value(lower_rvalue(*expr));
      auto pi = flat_or_tinfo(port.getType());
      auto ei = flat_or_tinfo(*expr->type);
      v       = materialize_conversion(v, ei.bits, ei.is_signed, pi.bits, pi.is_signed, value_width(*expr));
      clear_pending_loc();
      in_args.emplace_back(ref_name_of_raw(port.name), v);
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
  // `ref_name_of_raw`, not the raw name: an ESCAPED instance id (`\mem.a` — the
  // spelling cgen emits for a detupled memory's wrapper) carries a '.', which an
  // unquoted LNAST ref reads as a bundle FIELD PATH; the symbol table asserts on
  // a dotted var long before tolg gets to name the Sub. Ordinary names are
  // returned unchanged.
  auto result    = inst.name.empty() ? builder_.create_lnast_tmp() : Slang_context::ref_name_of_raw(inst.name);
  ln.add_child(fcall_idx, Lnast_node::create_ref(result));
  ln.add_child(fcall_idx, Lnast_node::create_ref(callee));
  for (const auto& [pname, v] : in_args) {
    auto arg = ln.add_child(fcall_idx, Lnast_ntype::create_store());
    ln.add_child(arg, Lnast_node::create_ref(pname));
    builder_.add_value_child_pub(arg, v);
  }

  // Bind outputs: a single-output callee's result is the value itself;
  // multi-output callees yield a tuple read by field name. A BUNDLE output is
  // per-leaf dotted ports on the Sub, so even a lone bundle output makes the
  // instance result MULTI-output (dotted tuple_get path, never bare result).
  const bool single_out = n_outputs_total == 1 && !any_bundle_out;
  for (const auto& oc : outs) {
    if (oc.bundle) {
      // M7: read each leaf via ONE tuple_get with the DOTTED name (the form
      // tolg matches via its quoted-string path). When the actual is itself a
      // leaf-carrying target (the parent's own bundle port, or a leaf-split
      // struct var), route LEAF-TO-LEAF via map_leaves — the flat
      // reassemble-then-resplit below unions every child output's cone into
      // one Or node, which reads back as "every field depends on every
      // input" and manufactures false same-cycle rings between instances
      // (minion's intpipe<->dcache control bus). The flat path remains as
      // the general fallback (width-changing conversions, exotic actuals).
      auto pfields       = struct_port_fields(oc.port->getType());
      auto read_out_leaf = [&](const Struct_info::Field& f) {
        auto tg = builder_.add_child(Lnast_ntype::create_tuple_get());
        auto t  = builder_.create_lnast_tmp();
        ln.add_child(tg, Lnast_node::create_ref(t));
        ln.add_child(tg, Lnast_node::create_ref(result));
        ln.add_child(tg, Lnast_node::create_const(absl::StrCat(oc.port->name, ".", f.name)));
        return t;
      };
      {
        const slang::ast::Expression* pe = oc.expr;
        while (pe->kind == ExpressionKind::Conversion) {
          pe = &pe->as<slang::ast::ConversionExpression>().operand();
        }
        bool routed = false;
        if (pe->kind == ExpressionKind::NamedValue && flat_or_tinfo(*pe->type).bits == flat_or_tinfo(oc.port->getType()).bits) {
          const auto& asym      = pe->as<slang::ast::NamedValueExpression>().symbol;
          bool        noted     = false;
          auto        note_once = [&]() {
            if (!noted) {
              note_write(asym, current_assign_nonblocking_, oc.expr->sourceRange.start());
              noted = true;
            }
          };
          if (const auto* bsi = bundle_port_of(asym)) {
            const auto afields = bsi->fields;  // copy: builder calls can rehash the map
            auto       base    = bundle_port_body_base(asym);
            routed = map_leaves(afields, pfields, read_out_leaf, [&](const Struct_info::Field& tf, const std::string& v) {
              note_once();
              emit_leaf_store(absl::StrCat(base, ".", tf.name), v);
            });
          } else if (is_scalar_struct_var(asym)) {
            if (!declared_.contains(&asym)) {
              declare_value_symbol(asym, /*force_reg=*/false);
            }
            if (auto it = struct_var_info_.find(&asym); it != struct_var_info_.end()) {
              const bool a_is_tuple = it->second.is_tuple;
              const auto afields    = it->second.fields;  // copy: builder calls can rehash the map
              auto       aname      = lname_of(asym);
              routed = map_leaves(afields, pfields, read_out_leaf, [&](const Struct_info::Field& tf, const std::string& v) {
                note_once();
                if (a_is_tuple) {
                  emit_struct_field_set(aname, tf.name, v);
                } else {
                  emit_leaf_store(absl::StrCat(aname, ".", tf.name), v);
                }
              });
            }
          }
        }
        if (routed) {
          continue;
        }
      }
      std::string acc;
      for (const auto& f : pfields) {
        auto tg = builder_.add_child(Lnast_ntype::create_tuple_get());
        auto t  = builder_.create_lnast_tmp();
        ln.add_child(tg, Lnast_node::create_ref(t));
        ln.add_child(tg, Lnast_node::create_ref(result));
        ln.add_child(tg, Lnast_node::create_const(absl::StrCat(oc.port->name, ".", f.name)));
        auto placed = to_pattern(t, f.bits, f.is_signed);
        if (f.off != 0) {
          placed = builder_.create_shl_stmts(placed, std::to_string(f.off));
        }
        acc = acc.empty() ? placed : builder_.create_bit_or_stmts({acc, placed});
      }
      auto pi = flat_or_tinfo(oc.port->getType());
      auto ei = flat_or_tinfo(*oc.expr->type);
      assign_to(*oc.expr,
                materialize_conversion(acc.empty() ? std::string{"0"} : acc, pi.bits, pi.is_signed, ei.bits, ei.is_signed));
      continue;
    }
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
    // binding entirely). Guard: prp-v2prp2v/prp-simfail-instance_out_struct_ident.
    assign_to(*oc.expr, materialize_conversion(v, pi.bits, pi.is_signed, ei.bits, ei.is_signed));
  }
  clear_pending_loc();
}

void Slang_context::lower_unknown_instance(const slang::ast::UninstantiatedDefSymbol& inst, const std::vector<bool>& conn_is_out) {
  std::string callee{inst.definitionName};
  if (callee.empty()) {
    emit_unsupported(inst.location,
                     "unknown-module",
                     std::string("instance '") + std::string(inst.name) + "' has no definition name");
    return;
  }
  auto conns = inst.getPortConnections();
  auto names = inst.getPortNames();

  if (unknown_warned_.insert(callee).second) {
    emit_warning(slang::SourceRange(inst.location, inst.location), "unknown-module-blackbox", "unsupported",
                 std::string("module '") + callee
                     + "' has no definition; kept as a blackbox instance (port directions inferred, an `import` is "
                       "emitted on the pyrope output)",
                 "provide the module source, or supply a matching pyrope module at recompile time");
  }
  if (!inst.paramExpressions.empty()) {
    emit_warning(slang::SourceRange(inst.location, inst.location),
                 "unknown-module-params",
                 "unsupported",
                 std::string("parameter bindings of blackbox instance '") + std::string(inst.name) + "' ('" + callee
                     + "') are dropped (no definition to bind against)");
  }

  proc_kind_ = Proc_kind::none;

  // Mirror lower_instance, minus everything that needs the definition: no
  // port types (values pass self-determined, no materialize_conversion) and
  // the connection count stands in for the callee's output arity.
  struct Out_conn {
    std::string                   name;
    const slang::ast::Expression* expr;
  };
  std::vector<std::pair<std::string, std::string>> in_args;  // (port, value)
  std::vector<Out_conn>                            outs;

  for (size_t i = 0; i < conns.size(); ++i) {
    const auto* pe = unknown_conn_expr(conns[i]);
    if (pe == nullptr) {  // unconnected `.p()`
      continue;
    }
    std::string_view pname = i < names.size() ? names[i] : std::string_view{};
    if (pname.empty()) {
      emit_unsupported(inst.location,
                       "unknown-module-ordered-conn",
                       std::string("instance '") + std::string(inst.name) + "' of unknown module '" + callee
                           + "' uses ordered port connections (name/direction inference needs named ports)",
                       "use named `.port(expr)` connections");
      return;
    }
    const auto& expr = *pe;
    if (i < conn_is_out.size() && conn_is_out[i]) {
      outs.push_back({std::string(pname), &expr});
    } else {
      set_pending_loc(expr.sourceRange);
      auto v = to_int_value(lower_rvalue(expr));
      clear_pending_loc();
      in_args.emplace_back(std::string(pname), v);
    }
  }

  auto& ln = *builder_.lnast;
  set_pending_loc(inst.location);
  auto fcall_idx = builder_.add_child(Lnast_ntype::create_func_call());
  // `ref_name_of_raw`, not the raw name: an ESCAPED instance id (`\mem.a` — the
  // spelling cgen emits for a detupled memory's wrapper) carries a '.', which an
  // unquoted LNAST ref reads as a bundle FIELD PATH; the symbol table asserts on
  // a dotted var long before tolg gets to name the Sub. Ordinary names are
  // returned unchanged.
  auto result    = inst.name.empty() ? builder_.create_lnast_tmp() : Slang_context::ref_name_of_raw(inst.name);
  ln.add_child(fcall_idx, Lnast_node::create_ref(result));
  ln.add_child(fcall_idx, Lnast_node::create_ref(callee));
  for (const auto& [pname, v] : in_args) {
    auto arg = ln.add_child(fcall_idx, Lnast_ntype::create_store());
    ln.add_child(arg, Lnast_node::create_ref(pname));
    builder_.add_value_child_pub(arg, v);
  }

  // Output arity is a guess: every CONNECTED inferred output is assumed to be
  // a real callee output (SRAM-macro style blackboxes connect them all), so
  // exactly one binds the call result directly and several read tuple fields.
  const bool single_out = outs.size() == 1;
  for (const auto& oc : outs) {
    std::string v;
    if (single_out) {
      v = result;
    } else {
      auto tg = builder_.add_child(Lnast_ntype::create_tuple_get());
      auto t  = builder_.create_lnast_tmp();
      ln.add_child(tg, Lnast_node::create_ref(t));
      ln.add_child(tg, Lnast_node::create_ref(result));
      ln.add_child(tg, Lnast_node::create_const(oc.name));
      v = t;
    }
    assign_to(*oc.expr, v);
  }
  clear_pending_loc();

  builder_.lnast->add_external_module(callee);
}
