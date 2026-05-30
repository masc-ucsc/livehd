//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnastfmt.hpp"

#include <format>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "perf_tracing.hpp"

static Pass_plugin sample("Pass_lnastfmt", Pass_lnastfmt::setup);

Pass_lnastfmt::Pass_lnastfmt(const Eprp_var& var) : Pass("pass.lnastfmt", var) {}

void Pass_lnastfmt::setup() {
  Eprp_method m1("pass.lnastfmt", "Validate LNAST syntax; leave the tree unchanged", &Pass_lnastfmt::fmt_begin);
  register_pass(m1);
}

void Pass_lnastfmt::fmt_begin(Eprp_var& var) {
  TRACE_EVENT("pass", "lnastfmt");

  Pass_lnastfmt p(var);

  for (const auto& ln : var.lnasts) {
    p.validate(ln.get());
  }
}

// Location string matched to the `lnast.dump` columns so the user can jump
// straight from the error to the offending node:
//   `[L,P pos1-pos2 line N @ fname]` — L/P is the tree index (level,pos)
//   printed by dump as `(L,P)`, pos1-pos2 is the dump's leading range,
//   line/fname come from the source token when available.
static std::string node_loc(const Lnast* ln, const Lnast_nid& it) {
  const auto  loc   = ln->get_loc(it);
  const auto  fname = ln->get_fname(it);
  std::string s     = std::format("[{},{} pos {}-{}", level_of(it), pos_of(it), loc.pos1, loc.pos2);
  if (loc.line != 0) {
    s += std::format(" line {}", loc.line);
  }
  if (!fname.empty()) {
    s += std::format(" @ {}", fname);
  }
  s += "]";
  return s;
}

// A `ref` carries a single variable name. Allowed shapes (today):
//   - tmp ref      : `___<digits>` (Slice 1; will move to `Tree_index` per §13)
//   - quoted ident : `` `…` `` — Pyrope's escape for names containing
//                    characters outside `[A-Za-z0-9_]`. Producers must keep
//                    the backticks ONLY when the name actually has a
//                    special character; ``a`` is normalized to `a`
//                    (prp2lnast strips the escape).
//   - dotted path  : `[$%#]?<id>(\.<id>)*` where `<id>` is
//                    `[A-Za-z_][A-Za-z0-9_]*`. The optional leading `$`/`%`/`#`
//                    is not valid ref text; direction/storage lives in
//                    structural metadata instead of a textual prefix.
// Anything else (commas, spaces, colons, slashes, …) is a producer bug —
// most often a `get_text(lvalue)` that swallowed surrounding syntax (type
// cast, comma list, brackets) or a stale escape that wasn't stripped at
// lowering time.
static bool is_valid_ref_text(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  if (name.front() == '`') {
    // Pyrope backtick-escaped name: must be terminated and must actually
    // need the escape — i.e. the inner text contains at least one char
    // that's not `[A-Za-z0-9_]` (otherwise prp2lnast should have stripped
    // the backticks). Reject ``a`` to keep the LNAST canonical.
    if (name.size() < 2 || name.back() != '`') {
      return false;
    }
    auto inner = name.substr(1, name.size() - 2);
    if (inner.empty()) {
      return false;
    }
    for (char ch : inner) {
      bool plain = (ch == '_') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
      if (!plain) {
        return true;  // escape needed — keep it
      }
    }
    return false;  // pure alnum/underscore — should have been stripped
  }
  size_t i = 0;
  // ___<digits> tmp form
  if (name.size() - i >= 4 && name.substr(i, 3) == "___") {
    for (size_t j = i + 3; j < name.size(); ++j) {
      if (name[j] < '0' || name[j] > '9') {
        return false;
      }
    }
    return name.size() > i + 3;
  }
  // Dotted identifier path
  bool start_of_segment = true;
  for (size_t j = i; j < name.size(); ++j) {
    char ch = name[j];
    if (ch == '.') {
      if (start_of_segment) {
        return false;  // empty segment
      }
      start_of_segment = true;
      continue;
    }
    bool ok = (ch == '_') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (!start_of_segment && ch >= '0' && ch <= '9');
    if (!ok) {
      return false;
    }
    start_of_segment = false;
  }
  return !start_of_segment;
}

// Returns true if a ref-child at child position 0 of a parent of this type
// is a write target (LHS / dst) — i.e., the parent produces or rebinds the
// variable named by its first child. Anything else falls through to "read".
//
// Notes on the ambiguous cases:
//   - `tuple_set`: position 0 names the bundle being mutated; we count it as
//     a write because the named variable is rebound (Bundle::set() replaces
//     entries) and any post-tuple_set reader sees the new value.
//   - `attr_set`: position 0 is the variable whose attribute is being set.
//     Treated as a write because it is the LNAST shape that *introduces* the
//     variable for `mut x = …` (prp2lnast emits `attr_set X type mut`
//     before the first `assign X = …`).
//   - `for`: position 0 is the iteration variable (bound for the loop body).
//   - `func_def`: position 0 is the function name.
//   - `if` / `while` / `cassert`: ALL ref children are reads (no write at
//     position 0), so this returns false for those — handled by the caller.
static bool parent_writes_pos0(Lnast_ntype::Lnast_ntype_int pt) {
  return Lnast_ntype::is_store(pt) || Lnast_ntype::is_declare(pt) || Lnast_ntype::is_dp_assign(pt)
         || Lnast_ntype::is_delay_assign(pt)
         || Lnast_ntype::is_log_and(pt) || Lnast_ntype::is_log_or(pt) || Lnast_ntype::is_log_not(pt) || Lnast_ntype::is_bit_and(pt)
         || Lnast_ntype::is_bit_or(pt) || Lnast_ntype::is_bit_not(pt) || Lnast_ntype::is_bit_xor(pt) || Lnast_ntype::is_red_and(pt)
         || Lnast_ntype::is_red_or(pt) || Lnast_ntype::is_red_xor(pt) || Lnast_ntype::is_popcount(pt) || Lnast_ntype::is_plus(pt)
         || Lnast_ntype::is_minus(pt) || Lnast_ntype::is_mult(pt) || Lnast_ntype::is_div(pt) || Lnast_ntype::is_mod(pt)
         || Lnast_ntype::is_shl(pt) || Lnast_ntype::is_sra(pt) || Lnast_ntype::is_sext(pt) || Lnast_ntype::is_set_mask(pt)
         || Lnast_ntype::is_get_mask(pt) || Lnast_ntype::is_eq(pt) || Lnast_ntype::is_ne(pt) || Lnast_ntype::is_lt(pt)
         || Lnast_ntype::is_le(pt) || Lnast_ntype::is_gt(pt) || Lnast_ntype::is_ge(pt) || Lnast_ntype::is_is(pt)
         || Lnast_ntype::is_tuple_add(pt) || Lnast_ntype::is_tuple_get(pt)
         || Lnast_ntype::is_tuple_concat(pt) || Lnast_ntype::is_attr_set(pt) || Lnast_ntype::is_attr_get(pt)
         || Lnast_ntype::is_func_call(pt) || Lnast_ntype::is_func_def(pt) || Lnast_ntype::is_range(pt)
         || Lnast_ntype::is_func_does(pt) || Lnast_ntype::is_func_equals(pt) || Lnast_ntype::is_func_in(pt)
         || Lnast_ntype::is_func_has(pt) || Lnast_ntype::is_func_case(pt) || Lnast_ntype::is_func_break(pt)
         || Lnast_ntype::is_func_continue(pt) || Lnast_ntype::is_func_return(pt) || Lnast_ntype::is_for(pt);
}

// Walk the tree and flag any tmp ref (`___N`) that is read but never written
// by any statement in the same LNAST. This catches producer bugs where an op
// is emitted referencing a tmp whose producing statement was dropped or
// emitted *after* the consumer (`if ___2 { … } ne ___2 = i!=2` — constprop's
// fixed-point can't fold the if because `___2` is set later in source order;
// once classify_statement drops the `ne`, the `if` is left dangling).
//
// We deliberately restrict this to tmps. Named variables can be implicitly
// declared by parent scopes / function parameters / module ports, and a
// whole-tree read-without-write check would flag those false-positives.
// Tmps are LNAST-internal: every tmp consumer must have a matching producer
// in the same tree.
static void check_unwritten_tmps(const Lnast* ln) {
  std::unordered_set<std::string>                                    written;
  std::unordered_map<std::string, std::pair<Lnast_nid, std::string>> read_first;

  for (const Lnast_nid& it : ln->depth_preorder()) {
    if (!Lnast_ntype::is_ref(ln->get_type(it))) {
      continue;
    }
    auto name = std::string(ln->get_name(it));
    if (!Lnast::is_tmp(name)) {
      continue;
    }
    auto parent = ln->get_parent(it);
    if (parent.is_invalid()) {
      continue;
    }
    bool is_first_child = (ln->get_first_child(parent) == it);
    auto pt             = ln->get_type(parent);

    // tuple_add entry-form: an inner `store(ref:key, val)` whose child[0] is a
    // *field name*, not a variable. A tmp shouldn't appear as a key in practice,
    // but skip just in case so we don't over-flag.
    if (Lnast_ntype::is_store(pt) && is_first_child) {
      auto grand = ln->get_parent(parent);
      if (!grand.is_invalid()) {
        auto gt = ln->get_type(grand);
        if (Lnast_ntype::is_tuple_add(gt)) {
          continue;
        }
      }
    }

    if (is_first_child && parent_writes_pos0(pt)) {
      written.insert(std::string(name));
    } else {
      auto key = std::string(name);
      if (!read_first.contains(key)) {
        read_first.emplace(key, std::make_pair(it, std::string(Lnast_ntype::debug_name(ln->get_type(parent)))));
      }
    }
  }

  // Sort for deterministic error reporting (the unordered_map iteration order
  // varies, which would make CI failures non-reproducible by name).
  std::set<std::string> dangling;
  for (const auto& kv : read_first) {
    if (!written.contains(kv.first)) {
      dangling.insert(kv.first);
    }
  }
  for (const auto& name : dangling) {
    const auto& info = read_first.at(name);
    Pass::error(
        "lnastfmt: {} tmp ref '{}' is read (under {}) but never written by any statement "
        "in this LNAST. Producer dropped its assignment, or producer emits the consumer "
        "before the producing statement (e.g. an `if cond` whose `cond = …` lands as a "
        "later sibling — fix prp2lnast to emit the cond's compute statements *before* "
        "the `if` node).",
        node_loc(ln, info.first),
        name,
        info.second);
    return;
  }
}

// Task 1t — declare-once. The storage kind of a `declare` is the FIRST
// space-separated token of its mode const ("const", "mut", "reg", "type", …).
static bool declare_mode_is_const_storage(std::string_view mode) {
  auto sp  = mode.find(' ');
  auto tok = (sp == std::string_view::npos) ? mode : mode.substr(0, sp);
  return tok == "const";
}

// A `const`-storage variable may be `declare`d only once per lexical scope (one
// `stmts` block). `mut`/`reg` redeclares are legal — they reset the binding
// (e.g. `mut counter = 1` appearing before each loop in matrix.prp) — and nested
// scopes (if/for/while/func_def bodies) each get a fresh frame so an inner
// declare may shadow an outer one (scope_simple.prp). The check runs on the live
// tree (constprop has already pruned dead branches), so only reachable
// redeclares are seen — a `const x` in a then-branch and a separate `const x` in
// the else-branch live in distinct frames and are not flagged.
static void check_declare_once(const Lnast* ln, const Lnast_nid& scope_stmts) {
  std::unordered_set<std::string> const_here;
  for (auto c = ln->get_first_child(scope_stmts); !c.is_invalid(); c = ln->get_sibling_next(c)) {
    if (Lnast_ntype::is_declare(ln->get_type(c))) {
      auto c0 = ln->get_first_child(c);
      auto c1 = c0.is_invalid() ? c0 : ln->get_sibling_next(c0);
      auto c2 = c1.is_invalid() ? c1 : ln->get_sibling_next(c1);
      if (!c0.is_invalid() && !c2.is_invalid() && Lnast_ntype::is_ref(ln->get_type(c0))
          && Lnast_ntype::is_const(ln->get_type(c2)) && declare_mode_is_const_storage(ln->get_name(c2))) {
        auto name = std::string(ln->get_name(c0));
        if (const_here.contains(name)) {
          Pass::error("lnastfmt: {} redeclaration of `const` variable '{}' in the same scope (a const may be declared only once)",
                      node_loc(ln, c),
                      name);
          return;
        }
        const_here.insert(name);
      }
    }
    // Recurse into nested scopes: any `stmts` child of a control-flow statement
    // (if/for/while/func_def) starts a fresh frame.
    for (auto cc = ln->get_first_child(c); !cc.is_invalid(); cc = ln->get_sibling_next(cc)) {
      if (Lnast_ntype::is_stmts(ln->get_type(cc))) {
        check_declare_once(ln, cc);
      }
    }
  }
}

void Pass_lnastfmt::validate(const Lnast* ln) {
  // Validation-only pass. Any normalization (SSA stripping, tuple rebuild,
  // …) must be safe for every producer before it can run here — until then
  // lnastfmt only checks and leaves the LNAST untouched.

  // ── top-level io-shape invariant ────────────────────────────────────────
  // Each Lnast top may have at most ONE io child. When present, it must be
  // the *first* child of top — downstream consumers (try_eval_comb_call,
  // lnast.dump, codegen) assume io+stmts in that order. Zero io children is
  // fine (signature-less callees, top-level module bodies).
  {
    const auto root = ln->get_root();
    if (!root.is_invalid() && Lnast_ntype::is_top(ln->get_type(root))) {
      int       io_count = 0;
      Lnast_nid first_io;
      bool      first_seen = false;
      Lnast_nid first_child;
      for (auto c : ln->children(root)) {
        if (!first_seen) {
          first_child = c;
          first_seen  = true;
        }
        if (Lnast_ntype::is_io(ln->get_type(c))) {
          if (io_count == 0) {
            first_io = c;
          }
          ++io_count;
        }
      }
      if (io_count > 1) {
        Pass::error("lnastfmt: {} top has {} io children — at most one is allowed", node_loc(ln, first_io), io_count);
        return;
      }
      if (io_count == 1 && !first_seen) {
        // unreachable: io_count would have stayed 0
      }
      if (io_count == 1 && first_seen && !(first_io == first_child)) {
        Pass::error("lnastfmt: {} io must be the first child of top", node_loc(ln, first_io));
        return;
      }
    }
  }

  for (const Lnast_nid& it : ln->depth_preorder()) {
    const auto type = ln->get_type(it);

    if (Lnast_ntype::is_ref(type)) {
      const auto name = ln->get_name(it);
      if (!is_valid_ref_text(name)) {
        Pass::error(
            "lnastfmt: {} ref text '{}' is not a valid identifier — must be a single name "
            "(`tmp ___N`, `[$%#]?<id>(.<id>)*` over alnum/underscore, or "
            "`` `…` `` only if the name carries non-alnum characters). "
            "Commas/spaces/colons typically mean the producer captured surrounding syntax "
            "(type cast, lvalue list, …) into the ref; a backtick-escaped pure-alnum name "
            "(like ``a``) means the producer didn't normalize it.",
            node_loc(ln, it),
            name);
        return;
      }
    }

    if (Lnast_ntype::is_const(type)) {
      const auto name = ln->get_name(it);
      if (name == "__create_flop" || name == "__last_value") {
        Pass::error(
            "lnastfmt: {} const '{}' appears in LNAST; migrate the producer to `attr_set X storage reg` (§15.1) or `delay_assign` "
            "(§15.2)",
            node_loc(ln, it),
            name);
        return;
      }
    }

    if (Lnast_ntype::is_delay_assign(type)) {
      auto c0 = ln->get_first_child(it);
      auto c1 = c0.is_invalid() ? c0 : ln->get_sibling_next(c0);
      auto c2 = c1.is_invalid() ? c1 : ln->get_sibling_next(c1);
      if (c0.is_invalid() || c1.is_invalid() || c2.is_invalid() || !ln->get_sibling_next(c2).is_invalid()) {
        Pass::error("lnastfmt: {} delay_assign must have exactly 3 children (dst, src, offset) per §15.2", node_loc(ln, it));
        return;
      }
      if (!Lnast_ntype::is_ref(ln->get_type(c0)) || !Lnast::is_tmp(ln->get_name(c0))) {
        Pass::error("lnastfmt: {} delay_assign child 0 {} must be a tmp ref (___<n>), got '{}' of type {}",
                    node_loc(ln, it),
                    node_loc(ln, c0),
                    ln->get_name(c0),
                    Lnast_ntype::debug_name(ln->get_type(c0)));
        return;
      }
      if (!Lnast_ntype::is_ref(ln->get_type(c1))) {
        Pass::error("lnastfmt: {} delay_assign child 1 {} must be a ref (declared variable name), got type {}",
                    node_loc(ln, it),
                    node_loc(ln, c1),
                    Lnast_ntype::debug_name(ln->get_type(c1)));
        return;
      }
      if (!Lnast_ntype::is_const(ln->get_type(c2)) && !Lnast_ntype::is_ref(ln->get_type(c2))) {
        Pass::error("lnastfmt: {} delay_assign child 2 {} (offset) must be a const or comptime ref, got type {}",
                    node_loc(ln, it),
                    node_loc(ln, c2),
                    Lnast_ntype::debug_name(ln->get_type(c2)));
        return;
      }
    }

    // §11.5 (partial): structural shape of attr_set / attr_get. The full
    // §11.5 semantic checks (write-once, attr-after-read freeze, mid-body
    // declaration rejection) need post-constprop ST; this entry covers the
    // shape-only invariants that prp2lnast must produce regardless.
    if (Lnast_ntype::is_attr_set(type)) {
      auto c0 = ln->get_first_child(it);
      auto c1 = c0.is_invalid() ? c0 : ln->get_sibling_next(c0);
      auto c2 = c1.is_invalid() ? c1 : ln->get_sibling_next(c1);
      if (c0.is_invalid() || c1.is_invalid() || c2.is_invalid()) {
        Pass::error("lnastfmt: {} attr_set must have at least 3 children (target, attr_key, value)", node_loc(ln, it));
        return;
      }
      if (!Lnast_ntype::is_ref(ln->get_type(c0))) {
        Pass::error("lnastfmt: {} attr_set child 0 {} must be a ref (target var), got {}",
                    node_loc(ln, it),
                    node_loc(ln, c0),
                    Lnast_ntype::debug_name(ln->get_type(c0)));
        return;
      }
      if (!Lnast_ntype::is_const(ln->get_type(c1))) {
        Pass::error("lnastfmt: {} attr_set child 1 {} (attr_key) must be a const, got {}",
                    node_loc(ln, it),
                    node_loc(ln, c1),
                    Lnast_ntype::debug_name(ln->get_type(c1)));
        return;
      }
    }
    if (Lnast_ntype::is_attr_get(type)) {
      auto c0 = ln->get_first_child(it);
      auto c1 = c0.is_invalid() ? c0 : ln->get_sibling_next(c0);
      auto c2 = c1.is_invalid() ? c1 : ln->get_sibling_next(c1);
      if (c0.is_invalid() || c1.is_invalid() || c2.is_invalid()) {
        Pass::error("lnastfmt: {} attr_get must have at least 3 children (dst, target, attr_key)", node_loc(ln, it));
        return;
      }
      if (!Lnast_ntype::is_ref(ln->get_type(c0)) || !Lnast::is_tmp(ln->get_name(c0))) {
        Pass::error("lnastfmt: {} attr_get child 0 {} must be a tmp ref (___<n>), got {} '{}'",
                    node_loc(ln, it),
                    node_loc(ln, c0),
                    Lnast_ntype::debug_name(ln->get_type(c0)),
                    ln->get_name(c0));
        return;
      }
      // child 1 (target) is normally a ref, but constprop / runner emit-with-
      // fold may inline a const value (`a.[crand]` after `const a = false`
      // becomes `false.[crand]`) before the post-pipeline lnastfmt runs. Both
      // shapes are legal post-fold.
      if (!Lnast_ntype::is_ref(ln->get_type(c1)) && !Lnast_ntype::is_const(ln->get_type(c1))) {
        Pass::error("lnastfmt: {} attr_get child 1 {} (target) must be a ref or const, got {}",
                    node_loc(ln, it),
                    node_loc(ln, c1),
                    Lnast_ntype::debug_name(ln->get_type(c1)));
        return;
      }
      if (!Lnast_ntype::is_const(ln->get_type(c2))) {
        Pass::error("lnastfmt: {} attr_get child 2 {} (attr_key) must be a const, got {}",
                    node_loc(ln, it),
                    node_loc(ln, c2),
                    Lnast_ntype::debug_name(ln->get_type(c2)));
        return;
      }
    }

    // Task 1t — store(var, level0..levelN, value): child 0 is the LHS var
    // (a ref), and there is at least one more child (the value). No type slot
    // (a store can never retype — "type only at declaration").
    if (Lnast_ntype::is_store(type)) {
      auto c0 = ln->get_first_child(it);
      if (c0.is_invalid() || ln->get_sibling_next(c0).is_invalid()) {
        Pass::error("lnastfmt: {} store must have at least 2 children (var, value)", node_loc(ln, it));
        return;
      }
      if (!Lnast_ntype::is_ref(ln->get_type(c0))) {
        Pass::error("lnastfmt: {} store child 0 {} (var) must be a ref, got {}",
                    node_loc(ln, it),
                    node_loc(ln, c0),
                    Lnast_ntype::debug_name(ln->get_type(c0)));
        return;
      }
    }

    // Task 1t — declare(var, type, const(mode), [value]): child 0 a ref (var),
    // child 1 a type node (prim_type_*/comp_type_*/expr_type/none_type), child
    // 2 a const (the mode). An optional 4th child is the init value.
    if (Lnast_ntype::is_declare(type)) {
      auto c0 = ln->get_first_child(it);
      auto c1 = c0.is_invalid() ? c0 : ln->get_sibling_next(c0);
      auto c2 = c1.is_invalid() ? c1 : ln->get_sibling_next(c1);
      if (c0.is_invalid() || c1.is_invalid() || c2.is_invalid()) {
        Pass::error("lnastfmt: {} declare must have at least 3 children (var, type, mode)", node_loc(ln, it));
        return;
      }
      if (!Lnast_ntype::is_ref(ln->get_type(c0))) {
        Pass::error("lnastfmt: {} declare child 0 {} (var) must be a ref, got {}",
                    node_loc(ln, it),
                    node_loc(ln, c0),
                    Lnast_ntype::debug_name(ln->get_type(c0)));
        return;
      }
      // child 1 is a type node, or a `ref` (a named type — task 1t, the ref
      // resolves to the named type's symbol-table binding).
      if (!Lnast_ntype::is_type(ln->get_type(c1)) && !Lnast_ntype::is_ref(ln->get_type(c1))) {
        Pass::error("lnastfmt: {} declare child 1 {} (type) must be a type node or named-type ref, got {}",
                    node_loc(ln, it),
                    node_loc(ln, c1),
                    Lnast_ntype::debug_name(ln->get_type(c1)));
        return;
      }
      if (!Lnast_ntype::is_const(ln->get_type(c2))) {
        Pass::error("lnastfmt: {} declare child 2 {} (mode) must be a const, got {}",
                    node_loc(ln, it),
                    node_loc(ln, c2),
                    Lnast_ntype::debug_name(ln->get_type(c2)));
        return;
      }
    }

    // Task 1t — `assign` was deleted; statement/field/signature writes are all
    // `store` (validated leniently above: child0=ref, ≥2 children, any arity).
    // Only `dp_assign` keeps the strict 2-child (+ legacy 3-child) shape here.
    if (Lnast_ntype::is_dp_assign(type)) {
      auto c0 = ln->get_first_child(it);
      if (c0.is_invalid() || !Lnast_ntype::is_ref(ln->get_type(c0))) {
        if (c0.is_invalid()) {
          Pass::error("lnastfmt: {} {} LHS is missing", node_loc(ln, it), Lnast_ntype::debug_name(type));
        } else {
          Pass::error("lnastfmt: {} {} LHS {} must be a ref, got {} '{}'",
                      node_loc(ln, it),
                      Lnast_ntype::debug_name(type),
                      node_loc(ln, c0),
                      Lnast_ntype::debug_name(ln->get_type(c0)),
                      ln->get_name(c0));
        }
        return;
      }
      auto c1 = ln->get_sibling_next(c0);
      if (c1.is_invalid()) {
        Pass::error("lnastfmt: {} {} with LHS '{}' is missing its RHS (expected exactly 2 children)",
                    node_loc(ln, it),
                    Lnast_ntype::debug_name(type),
                    ln->get_name(c0));
        return;
      }
      auto c2 = ln->get_sibling_next(c1);
      if (!c2.is_invalid()) {
        // The only legitimate 3-child `assign` is a signature entry:
        // `(assign ref default type)` inside an inputs/outputs tuple_add that
        // is a direct child of a func_def (pre-extraction shape) or io
        // (post-extraction shape), or a nested tuple-typed slot using the
        // same recursive shape (parent assign).
        bool ok     = false;
        auto parent = ln->get_parent(it);
        if (!parent.is_invalid() && Lnast_ntype::is_tuple_add(ln->get_type(parent))) {
          auto gp = ln->get_parent(parent);
          if (!gp.is_invalid()
              && (Lnast_ntype::is_func_def(ln->get_type(gp)) || Lnast_ntype::is_io(ln->get_type(gp))
                  || Lnast_ntype::is_store(ln->get_type(gp)))) {
            ok = true;
          }
        }
        if (!ok) {
          Pass::error("lnastfmt: {} {} with LHS '{}' has more than 2 children",
                      node_loc(ln, it),
                      Lnast_ntype::debug_name(type),
                      ln->get_name(c0));
          return;
        }
        // Trailing children beyond the type slot are not allowed.
        if (!ln->get_sibling_next(c2).is_invalid()) {
          Pass::error("lnastfmt: {} {} with LHS '{}' has more than 3 children in a signature position",
                      node_loc(ln, it),
                      Lnast_ntype::debug_name(type),
                      ln->get_name(c0));
          return;
        }
      }
    }
  }

  check_unwritten_tmps(ln);

  // Task 1t — declare-once: a `const` variable cannot be redeclared in the same
  // scope. Walk each top-level `stmts` block; nested scopes recurse.
  for (auto c = ln->get_first_child(ln->get_root()); !c.is_invalid(); c = ln->get_sibling_next(c)) {
    if (Lnast_ntype::is_stmts(ln->get_type(c))) {
      check_declare_once(ln, c);
    }
  }
}
