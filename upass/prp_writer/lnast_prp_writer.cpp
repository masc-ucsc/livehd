//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_prp_writer.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// ── Constructor ───────────────────────────────────────────────────────────────

Lnast_prp_writer::Lnast_prp_writer(std::ostream& _os, std::shared_ptr<Lnast> _lnast) : os(_os), lnast(std::move(_lnast)) {}

// ── Public entry point ────────────────────────────────────────────────────────

void Lnast_prp_writer::write_all() {
  depth     = 0;
  nid_stack = {};
  analyze_folding();  // decide which single-use temps to inline
  cur = lnast->get_root();
  write_node();
}

// ── Cursor helpers ────────────────────────────────────────────────────────────

bool Lnast_prp_writer::move_to_child() {
  auto child = lnast->get_child(cur);
  if (child.is_invalid()) {
    return false;
  }
  nid_stack.push(cur);
  cur = child;
  return true;
}

bool Lnast_prp_writer::move_to_sibling() {
  auto sib = lnast->get_sibling_next(cur);
  if (sib.is_invalid()) {
    return false;
  }
  cur = sib;
  return true;
}

void Lnast_prp_writer::move_to_parent() {
  cur = nid_stack.top();
  nid_stack.pop();
}

bool Lnast_prp_writer::is_last_child() const { return lnast->is_last_child(cur); }

std::string_view Lnast_prp_writer::current_text() const { return lnast->get_name(cur); }

Lnast_ntype::Lnast_ntype_int Lnast_prp_writer::current_ntype() const { return lnast->get_type(cur); }

// ── Output helpers ────────────────────────────────────────────────────────────

void Lnast_prp_writer::print(std::string_view s) { os << s; }

void Lnast_prp_writer::print_indent() {
  for (int i = 0; i < depth; ++i) {
    os << "  ";
  }
}

void Lnast_prp_writer::println(std::string_view s) {
  print_indent();
  os << s << "\n";
}

// ── Utilities ─────────────────────────────────────────────────────────────────

bool Lnast_prp_writer::is_tmp(std::string_view name) const {
  // A raw compiler temp (`%`-prefix), OR a name strip_prefix already mapped to
  // its emittable `t<id>` form.
  if (!name.empty() && name[0] == '%') {
    return true;
  }
  return emitted_tmp_names_.contains(std::string(name));
}

std::string Lnast_prp_writer::emit_name_for(std::string_view tmp) const {
  // One-time seed: every NON-temp ref name in the tree is reserved, so a
  // synthesised `t<id>` can never collide with a user identifier or port.
  if (!emit_names_seeded_) {
    emit_names_seeded_ = true;
    for (const auto& nid : lnast->depth_preorder()) {
      if (!Lnast_ntype::is_ref(lnast->get_type(nid))) {
        continue;
      }
      auto nm = lnast->get_name(nid);
      if (nm.empty() || is_tmp(nm)) {
        continue;  // a compiler temp — gets mapped to `t<id>`, not a reserved user name
      }
      used_emit_names_.insert(std::string(nm));
    }
  }
  auto cached = tmp_emit_names_.find(std::string(tmp));
  if (cached != tmp_emit_names_.end()) {
    return cached->second;
  }
  std::string base = "t" + std::string(tmp.substr(1));  // %pipe_o -> tpipe_o
  std::string cand = base;
  for (int m = 1; used_emit_names_.contains(cand); ++m) {
    cand = base + "_" + std::to_string(m);
  }
  used_emit_names_.insert(cand);
  emitted_tmp_names_.insert(cand);
  tmp_emit_names_.emplace(std::string(tmp), cand);
  return cand;
}

std::string_view Lnast_prp_writer::infix_symbol(Lnast_ntype::Lnast_ntype_int t) {
  using N = Lnast_ntype;
  switch (t) {
    case N::Lnast_ntype_plus   : return "+";
    case N::Lnast_ntype_minus  : return "-";
    case N::Lnast_ntype_mult   : return "*";
    case N::Lnast_ntype_div    : return "/";
    case N::Lnast_ntype_mod    : return "%";
    case N::Lnast_ntype_shl    : return "<<";
    case N::Lnast_ntype_sra    : return ">>";
    case N::Lnast_ntype_eq     : return "==";
    case N::Lnast_ntype_ne     : return "!=";
    case N::Lnast_ntype_lt     : return "<";
    case N::Lnast_ntype_le     : return "<=";
    case N::Lnast_ntype_gt     : return ">";
    case N::Lnast_ntype_ge     : return ">=";
    case N::Lnast_ntype_log_and: return "and";
    case N::Lnast_ntype_log_or : return "or";
    case N::Lnast_ntype_bit_and: return "&";
    case N::Lnast_ntype_bit_or : return "|";
    case N::Lnast_ntype_bit_xor: return "^";
    default                    : return "";
  }
}

bool Lnast_prp_writer::is_foldable_optype(Lnast_ntype::Lnast_ntype_int t) {
  using N = Lnast_ntype;
  if (!infix_symbol(t).empty()) {
    return true;  // every infix arithmetic/bitwise/logical/comparison op
  }
  switch (t) {
    case N::Lnast_ntype_log_not:
    case N::Lnast_ntype_bit_not:
    case N::Lnast_ntype_sext:
    case N::Lnast_ntype_get_mask:
    case N::Lnast_ntype_tuple_get:
    case N::Lnast_ntype_attr_get : return true;
    // store is foldable only as a plain copy (handled at the call site, which
    // checks the arity); set_mask/range/func_call/delay_assign are statement
    // forms, never inlined.
    default                      : return false;
  }
}

std::string Lnast_prp_writer::take_decl_keyword(std::string_view lhs) {
  auto it = pending_decl_.find(std::string(lhs));
  if (it == pending_decl_.end()) {
    return {};
  }
  auto kw = it->second;
  pending_decl_.erase(it);
  return kw;
}

std::string Lnast_prp_writer::decl_prefix(std::string_view lhs) {
  auto kw = take_decl_keyword(lhs);
  if (!kw.empty()) {
    declared_.insert(std::string(lhs));
    return kw + " ";
  }
  if (is_tmp(lhs)) {
    // An EMITTED compiler-temp — a multi-use `___x` net the writer did NOT inline
    // (folded single-use temps are inlined and never reach decl_prefix) — must be
    // DECLARED, not left as a bare `___x = …`.  A bare reserved-namespace name
    // re-parses as a compiler tmp and flows through the SSA tmp-rename /
    // value-number machinery, where it ALIASES an internally-minted temp and
    // silently inherits ITS kind (e.g. a single-bit `(x>>N)&1` net reads back as
    // `boolean`, so a later `(net) << k` fails typecheck).  Emit `const` to make
    // it an explicit, single-assignment external net — these `___x` are SSA temps
    // (written exactly once).
    if (declared_.count(std::string(lhs))) {
      return {};
    }
    declared_.insert(std::string(lhs));
    return "const ";
  }
  if (declared_.count(std::string(lhs))) {
    return {};
  }
  declared_.insert(std::string(lhs));
  return "mut ";
}

std::string Lnast_prp_writer::strip_prefix(std::string_view name) const {
  // Move the source's SSA-version names out of the recompile's PRIVATE `___ssa_`
  // namespace.  The reader hands the writer POST-SSA LNAST whose versioned names
  // (`active___ssa_1`) collide with the names the recompile's own SSA pass mints
  // when it re-versions `active` (`active___ssa_1` again) — yielding a self-assign
  // (`active___ssa_1 = active___ssa_1`, the "irrelevant assignment" error) or
  // dropped writes.  Rename `<base>___ssa_<N>` -> `<base>__w<N>`: still DISTINCT
  // per version (so a module that keeps two live versions, e.g. a FIFO, stays
  // correct) but outside the `___ssa_` namespace, so the recompile re-versions it
  // freely without collision.
  // Names that are not a plain Pyrope identifier (e.g. upass.detuple's per-field
  // memories `mem.field`, which carry a `.`) must be emitted as a backtick-escaped
  // identifier so the re-compile leg can re-parse them; the lexer strips the
  // backticks back to the identical name, so the round-trip is exact.
  // Only a `.` makes a body name a non-identifier here (upass.detuple's per-field
  // `mem.field` memories). Integer constants that also flow through this helper
  // (`6`, `0sb?`, `0xff`) must stay bare, so do NOT quote on other characters.
  auto quote = [](std::string s) -> std::string {
    // A name read from an escaped Verilog id arrives ALREADY backtick-quoted
    // (`` `ar.x` `` — the dlop quoted-identifier form). Emit it verbatim; wrapping
    // it again yields `` ``ar.x`` `` which the Pyrope lexer rejects (the v2prp
    // round-trip then fails to re-parse). Only a bare `.`-name needs quoting.
    if (s.size() >= 2 && s.front() == '`' && s.back() == '`') {
      return s;
    }
    return s.find('.') == std::string::npos ? s : "`" + s + "`";
  };
  // A `%`-prefixed compiler temp is not a legal Pyrope identifier — map it to an
  // emittable `t<id>` (collision-checked). A trailing `.field` (e.g. a detuple
  // temp `%t0.0`) keeps its field path on the mapped head, then quotes if needed.
  if (!name.empty() && name[0] == '%') {
    auto        dot  = name.find('.');
    std::string head = emit_name_for(dot == std::string_view::npos ? name : name.substr(0, dot));
    if (dot != std::string_view::npos) {
      head += std::string(name.substr(dot));
    }
    return quote(head);
  }
  auto pos = name.rfind("___ssa_");
  if (pos == std::string_view::npos) {
    return quote(std::string(name));
  }
  for (size_t i = pos + 7; i < name.size(); ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return quote(std::string(name));  // not a pure-digit suffix — leave intact
    }
  }
  if (pos + 7 >= name.size()) {
    return quote(std::string(name));  // bare `___ssa_` with no version — leave intact
  }
  std::string out(name.substr(0, pos));
  out += "__w";
  out += name.substr(pos + 7);
  return quote(out);
}

// ── Main dispatch ─────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_node() {
  using N = Lnast_ntype;
  switch (current_ntype()) {
    case N::Lnast_ntype_top         : write_top(); break;
    case N::Lnast_ntype_stmts       : write_stmts(); break;
    case N::Lnast_ntype_if          : write_if(); break;
    case N::Lnast_ntype_unique_if   : write_if(); break;  // prints `unique if`
    case N::Lnast_ntype_declare     : write_declare(); break;
    case N::Lnast_ntype_store       : write_store(); break;
    case N::Lnast_ntype_ref         : write_ref(); break;
    case N::Lnast_ntype_const       : write_const(); break;
    case N::Lnast_ntype_cassert     : write_cassert(); break;
    case N::Lnast_ntype_func_call   : write_func_call(); break;
    case N::Lnast_ntype_func_def    : write_func_def(); break;
    case N::Lnast_ntype_for         : write_for(); break;
    // timecheck (`x@[N]`) is an inert landing-cycle assertion. The timing it
    // carries is ALREADY re-emitted by the writer as the `stage[N]` declaration
    // and the `out:T@[N]` interface annotation, so the standalone statement is
    // redundant; and its SSA-renamed ref often names a stage var not yet
    // assigned at this point (`tmp@[3]` precedes `stage[3] tmp = …`), which
    // would forward-reference an undeclared name on re-parse. Drop it (the
    // `@[]` opt-out) — sound (inert) and loses no timing. See emits_nothing_stmt
    // so the statement loop leaves no blank line.
    case N::Lnast_ntype_timecheck   : break;
    case N::Lnast_ntype_tuple_add   : write_tuple_add(); break;
    case N::Lnast_ntype_tuple_concat: write_tuple_concat(); break;
    case N::Lnast_ntype_attr_set    : write_attr_set(); break;
    case N::Lnast_ntype_delay_assign: write_delay_assign(); break;
    case N::Lnast_ntype_set_mask    : write_set_mask(); break;
    case N::Lnast_ntype_range       : write_range(); break;
    case N::Lnast_ntype_type_spec   : write_type_spec(); break;
    // All value-producing ops share one statement wrapper; render_def_rhs()
    // spells the per-op RHS and inlines any single-use temp operands.
    case N::Lnast_ntype_plus        :
    case N::Lnast_ntype_minus       :
    case N::Lnast_ntype_mult        :
    case N::Lnast_ntype_div         :
    case N::Lnast_ntype_mod         :
    case N::Lnast_ntype_shl         :
    case N::Lnast_ntype_sra         :
    case N::Lnast_ntype_sext        :
    case N::Lnast_ntype_get_mask    :
    case N::Lnast_ntype_eq          :
    case N::Lnast_ntype_ne          :
    case N::Lnast_ntype_lt          :
    case N::Lnast_ntype_le          :
    case N::Lnast_ntype_gt          :
    case N::Lnast_ntype_ge          :
    case N::Lnast_ntype_log_and     :
    case N::Lnast_ntype_log_or      :
    case N::Lnast_ntype_log_not     :
    case N::Lnast_ntype_bit_and     :
    case N::Lnast_ntype_bit_or      :
    case N::Lnast_ntype_bit_xor     :
    case N::Lnast_ntype_bit_not     :
    case N::Lnast_ntype_tuple_get   :
    case N::Lnast_ntype_attr_get    : write_value_stmt(); break;
    default                         : {
      // Unknown node — record it (the pass fails the compile unless debug) and
      // emit a comment so the output stays parseable.
      emit_unimplemented(std::format("unhandled node type {} ({})",
                                     static_cast<int>(current_ntype()),
                                     Lnast_ntype::to_sv(current_ntype())));
      break;
    }
  }
}

// Record an unimplemented construct and emit the parseable marker inline at the
// cursor.  pass.prp_writer reads has_unimplemented() and, unless debug mode is
// on, turns it into a fatal diagnostic so the compile does not silently pass
// with a /* TODO */ stub in the generated Pyrope.
void Lnast_prp_writer::emit_unimplemented(std::string_view what) {
  unimplemented_.emplace_back(what);
  print(std::format("/* TODO: {} */", what));
}

// ── Structural ────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_top() {
  if (!move_to_child()) {
    return;
  }
  // Slang-origin module: the first child is an `io` node (port declarations)
  // followed by a `stmts` body sibling.  Emit a named comb/mod lambda.
  if (current_ntype() == Lnast_ntype::Lnast_ntype_io) {
    write_module();
    move_to_parent();
    return;
  }
  // Pyrope-origin bare file: no enclosing comb/fun declaration in the source;
  // explicit function definitions are emitted by write_func_def() instead.
  write_node();
  move_to_parent();
}

// ── Module (slang-origin io + body) ─────────────────────────────────────────

// Reconstruct a Verilog-derived module as a named Pyrope comb/mod.  Cursor sits
// on the `io` node (its sibling is the body `stmts`).  The header is emitted
// from the io subtree; the body is the following `stmts`, emitted inside the
// lambda braces (NOT brace-wrapped again — its parent is `top`, so write_stmts
// would not wrap it, but we walk it directly here to control indentation).
void Lnast_prp_writer::write_module() {
  Lnast_nid io_nid = cur;

  // File top-scope imports: one per instantiated submodule (a func_call callee
  // that is itself an emitted module).  `const X = import("X.X")` binds the
  // single pub entry so the cross-module call `X(...)` resolves on re-compile.
  if (known_modules_ != nullptr) {
    std::string              self(lnast->get_top_module_name());
    std::vector<std::string> imports;
    for (const auto& c : func_call_callees_) {
      if (c != self && known_modules_->count(c) != 0u) {
        imports.push_back(c);
      }
    }
    std::sort(imports.begin(), imports.end());
    for (const auto& nm : imports) {
      os << "const " << nm << " = import(\"" << nm << "." << nm << "\")\n";
    }
    if (!imports.empty()) {
      os << "\n";
    }
  }

  const bool is_mod = body_has_state(lnast->get_sibling_next(io_nid));
  // `pub … ::[lg="<name>"]` pins the generated module name to the source module
  // name verbatim, so the re-compile leg and LEC see the SAME identifier as the
  // golden .v.  Without it the lambda name `fun3` would be re-qualified by the
  // .prp filename (`trivial_if.fun3` → `trivial_if.fun3.fun3`, and a non-dotted
  // `chip_top` → `chip_top.chip_top`).  lg requires a `pub` lambda.
  print("pub ");
  print(is_mod ? "mod " : "comb ");
  print(lambda_name());
  // `lg` pins the module name; `hdl` marks this as a Verilog-imported unit so
  // the re-compile treats its plain regs as always_ff state (σ=0), not Pyrope
  // feedforward pipeline stages (the cross-cycle timing check would otherwise
  // fire when a reg q meets a combinational value).
  print(std::format("::[lg=\"{}\", hdl]", lnast->get_top_module_name()));
  emit_module_header(io_nid, is_mod);
  print(" {\n");
  ++depth;

  auto stmts_nid = lnast->get_sibling_next(io_nid);
  collect_folded_attrs(stmts_nid);  // gather reg/mem attrs to fold into declares
  if (!stmts_nid.is_invalid()) {
    // Compute the clock/reset-pin dependency CONE: every body net that
    // transitively feeds a `clock_pin`/`reset_pin` ref.  These are relocated
    // ahead of the reg declares (and excluded from the mut-hoist) so the `ref`
    // binds to a real driver, not a hoisted 0.  Closure over the combinational
    // drivers' read sets catches a multi-level derived clock
    // (`inv = ~gate; gclk = clk_b & inv`).
    pin_cone_ = pin_dep_nets_;
    if (!pin_dep_nets_.empty()) {
      std::unordered_map<std::string, Lnast_nid> body_driver;  // stripped lhs -> its (first) combinational driver
      for (auto c = lnast->get_child(stmts_nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
        auto c0 = lnast->get_child(c);
        if (c0.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(c0)) || !defines_child0(lnast->get_type(c))) {
          continue;
        }
        // A reg/latch declare is resolved in the declare pass, not relocatable;
        // a clock that reads a flop Q keeps the declared reg ref.
        if (lnast->get_type(c) == Lnast_ntype::Lnast_ntype_declare || is_folded_node(c) || emits_nothing_stmt(c)) {
          continue;
        }
        body_driver.emplace(std::string(strip_prefix(lnast->get_name(c0))), c);
      }
      std::vector<std::string> work(pin_dep_nets_.begin(), pin_dep_nets_.end());
      while (!work.empty()) {
        auto nm = work.back();
        work.pop_back();
        auto it = body_driver.find(nm);
        if (it == body_driver.end()) {
          continue;  // an interface port / reg / has no combinational body driver
        }
        std::unordered_set<std::string> reads;
        collect_driver_reads(it->second, reads);
        for (const auto& r : reads) {
          if (body_driver.count(r) && pin_cone_.insert(r).second) {
            work.push_back(r);
          }
        }
      }
    }
    // With the clock/reset cone known, decide which submodule output-port reads
    // collapse to `inst.port` at their uses (vs stay a feedback `wire`).
    analyze_instance_inline();
    // Pre-declare body `mut` vars that are WRITTEN but have no `declare` node.
    // Their first write would otherwise emit `mut X` inside whatever (possibly
    // nested) scope it lands in; a later write in a SIBLING scope then references
    // an out-of-scope X ("assignment to undeclared variable"). Hoisting
    // `mut X = 0` to the function top makes every write in-scope. Vars that DO
    // have a declare node (regs, memories, explicit `mut`) are skipped — they
    // are emitted by their declare (the reg/mem hoist pass below).
    {
      std::unordered_set<std::string>              top_decl, nonmut_decl, nested_mut_decl, store_lhs;
      std::unordered_map<std::string, std::string> nested_wire_decl;  // name -> rendered type (or "")
      auto                                         scan = [&](auto&& self, Lnast_nid n, bool top) -> void {
        for (auto c = lnast->get_child(n); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
          const auto t = lnast->get_type(c);
          auto       v = lnast->get_child(c);
          const bool v_ref = !v.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(v));
          if (v_ref && Lnast_ntype::is_declare(t)) {
            auto        nm   = std::string(strip_prefix(lnast->get_name(v)));
            auto        c1   = lnast->get_sibling_next(v);
            auto        c2   = c1.is_invalid() ? c1 : lnast->get_sibling_next(c1);
            std::string mode = (!c2.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c2)))
                                   ? std::string(lnast->get_name(c2))
                                   : std::string("mut");
            const bool  is_mut = mode.rfind("mut", 0) == 0;
            if (top) {
              top_decl.insert(nm);
            }
            if (!is_mut) {
              nonmut_decl.insert(nm);  // reg/latch/const: never hoist as `mut`
              // A `wire` is a module-scope net (it connects a driver to readers
              // across blocks), but the slang reader declares it at its first
              // textual READ — which may land inside a nested `if`, leaving the
              // module-scope store ("X = submod[...]") referencing an
              // out-of-scope name.  Hoist a nested `wire X:T` to the function top
              // (declaration only — the store stays the sole, position-
              // independent driver) and drop the in-place nested declare.
              if (!top && mode == "wire" && !nested_wire_decl.count(nm)) {
                nested_wire_decl.emplace(nm, c1.is_invalid() ? std::string{} : render_type_at(c1));
              }
            } else if (!top) {
              nested_mut_decl.insert(nm);
            }
          } else if (v_ref && Lnast_ntype::is_store(t) && !is_tmp(lnast->get_name(v))
                     && lnast->get_type(n) == Lnast_ntype::Lnast_ntype_stmts) {
            // Only a `store` whose PARENT is a `stmts` is a real statement write
            // that may need a hoisted `mut`.  A store nested under a `func_call`
            // (a `name = value` NAMED ARGUMENT) or a `tuple_add` (a named field)
            // is NOT a statement — collecting it wrongly hoisted bogus `mut a/b/
            // v/type = 0` lines from call sites like `mul(a=in1, b=in2)`.
            store_lhs.insert(std::string(strip_prefix(lnast->get_name(v))));
          }
          self(self, c, false);
        }
      };
      scan(scan, stmts_nid, true);
      // A combinational `mut` var written/declared in a nested scope but used in
      // SIBLING scopes must be declared at the function top (its first write
      // otherwise emits `mut` inside one scope, leaving sibling writes out of
      // scope). Hoist `mut X = 0` for: store-driven vars with no declare, and
      // vars with a NESTED `mut` declare. Skip top-declared / io / reg|latch|const.
      std::unordered_set<std::string> need;
      for (const auto& nm : store_lhs) {
        if (!top_decl.count(nm) && !nonmut_decl.count(nm) && !declared_.count(nm) && !pin_cone_.count(nm)
            && !instance_output_inlined_.count(nm)) {
          need.insert(nm);  // pin-cone nets are emitted (as `mut net = driver`) ahead of the declares, not hoisted to 0
        }
      }
      for (const auto& nm : nested_mut_decl) {
        if (!top_decl.count(nm) && !nonmut_decl.count(nm) && !declared_.count(nm) && !pin_cone_.count(nm)
            && !instance_output_inlined_.count(nm)) {
          need.insert(nm);
          suppress_decl_.insert(nm);  // its in-place nested `mut` declare is dropped
        }
      }
      std::vector<std::string> pre(need.begin(), need.end());
      std::sort(pre.begin(), pre.end());
      for (const auto& nm : pre) {
        print_indent();
        os << "mut " << nm << " = 0\n";
        declared_.insert(nm);
      }
      // Hoist nested `wire` declares to the function top as a bare `wire X:T`
      // (no `= 0` — the body store is the wire's single, position-independent
      // driver; a default init would make it multi-driven).  The in-place nested
      // declare is dropped via suppress_decl_.
      std::vector<std::string> wpre;
      for (const auto& [nm, ty] : nested_wire_decl) {
        if (!top_decl.count(nm) && !declared_.count(nm) && !pin_cone_.count(nm) && !instance_output_inlined_.count(nm)) {
          wpre.push_back(nm);
        }
      }
      std::sort(wpre.begin(), wpre.end());
      for (const auto& nm : wpre) {
        print_indent();
        os << "wire " << nm;
        if (const auto& ty = nested_wire_decl[nm]; !ty.empty()) {
          os << ":" << ty;
        }
        os << "\n";
        declared_.insert(nm);
        suppress_decl_.insert(nm);
      }
      // Any `wire` that has a real-statement store driver must not receive
      // write_declare's combinational `= 0` default (it is single-driver).
      wire_stored_.insert(store_lhs.begin(), store_lhs.end());
    }
    // Clock/reset-pin dependency CONE (e.g. a derived clock `gclk = clk_b &
    // gate`, or a multi-level `inv = ~gate; gclk = clk_b & inv`) must be DEFINED
    // before the reg declares that bind it via `clock_pin=ref <net>`.  Emit each
    // cone net's defining body statement now (in body order, so a dependency
    // lands before its consumer) as its first write -> `mut <net> = <driver>`,
    // and remember it so the body passes below skip it.
    std::unordered_set<int64_t> pin_net_emitted;
    if (!pin_cone_.empty()) {
      for (auto c = lnast->get_child(stmts_nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
        auto c0 = lnast->get_child(c);
        if (c0.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(c0)) || !defines_child0(lnast->get_type(c))) {
          continue;
        }
        if (lnast->get_type(c) == Lnast_ntype::Lnast_ntype_declare || is_folded_node(c) || emits_nothing_stmt(c)) {
          continue;
        }
        std::string lhs = std::string(strip_prefix(lnast->get_name(c0)));
        if (!pin_cone_.count(lhs) || declared_.count(lhs)) {
          continue;
        }
        print_indent();
        cur = c;
        write_node();  // decl_prefix sees the first write -> emits `mut <net> = <driver>`
        os << "\n";
        pin_net_emitted.insert(c.get_class_index().value);
      }
    }
    // Emit top-level `declare` statements first.  The slang reader places an
    // `attr_set` (e.g. `data.[fwd]=0`, a reg's `reset_pin`/`sync`/`initial`)
    // *before* the reg/memory `declare` it qualifies; Pyrope rejects an
    // attribute write to an undeclared variable, so hoisting the declares above
    // those attr writes makes the output reparse.  These declares carry no
    // forward-referencing init (the slang reset value is the `nil` sentinel,
    // suppressed), so reordering is semantically inert; the non-declare
    // statements keep their original order.
    for (int pass = 0; pass < 2; ++pass) {
      cur = stmts_nid;
      if (move_to_child()) {  // push stmts, cur -> first statement
        do {
          if (is_folded_node(cur) || emits_nothing_stmt(cur)) {
            continue;  // a temp def inlined at its single use, or a folded type_spec/stage decl
          }
          if (pin_net_emitted.count(cur.get_class_index().value)) {
            continue;  // a clock/reset-pin net driver already emitted ahead of the declares
          }
          const bool is_decl = current_ntype() == Lnast_ntype::Lnast_ntype_declare;
          if (is_decl != (pass == 0)) {
            continue;
          }
          // Skip attr_set statements that were folded into a declare's `:[…]`.
          if (current_ntype() == Lnast_ntype::Lnast_ntype_attr_set) {
            auto tgt = lnast->get_child(cur);
            if (!tgt.is_invalid() && folded_attrs_.count(std::string(strip_prefix(lnast->get_name(tgt))))) {
              continue;
            }
          }
          print_indent();
          write_node();
          os << "\n";
        } while (move_to_sibling());
        move_to_parent();  // cur -> stmts, pop
      }
    }
  }

  --depth;
  print_indent();
  print("}\n");
  cur = io_nid;  // restore for the caller's move_to_parent()
}

// Emit `(in0:T0, in1:T1, …) -> (out0:T0, …)` from the io node.  The io node has
// two `tuple_add` children: the first groups input ports, the second outputs.
// Each port is `store(ref(name), const(init|nil), type, [stages])`.
void Lnast_prp_writer::emit_port_group(Lnast_nid tup_nid, bool is_output, bool is_mod) {
  print("(");
  bool first = true;
  if (!tup_nid.is_invalid()) {
    for (auto port = lnast->get_child(tup_nid); !port.is_invalid(); port = lnast->get_sibling_next(port)) {
      auto name_nid = lnast->get_child(port);  // ref(name)
      if (name_nid.is_invalid()) {
        continue;
      }
      auto init_nid = lnast->get_sibling_next(name_nid);  // const(init|nil)
      auto type_nid = init_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(init_nid);
      // The optional trailing `stages` child rides after the (optional) type;
      // do not mistake it for the type slot.
      if (!type_nid.is_invalid() && Lnast_ntype::is_stages(lnast->get_type(type_nid))) {
        type_nid = Lnast_nid{};
      }
      if (!first) {
        print(", ");
      }
      // A var-arg param carries the `...` marker in its init const (pyrope
      // lambda signature); re-emit the spread so the template lambda reparses.
      const bool vararg = !init_nid.is_invalid() && lnast->get_type(init_nid) == Lnast_ntype::Lnast_ntype_const
                          && lnast->get_name(init_nid) == "...";
      if (vararg) {
        print("...");
      }
      auto pname = strip_prefix(lnast->get_name(name_nid));
      declared_.insert(std::string(pname));  // ports are pre-declared; body writes skip `mut`
      print(pname);
      if (!type_nid.is_invalid()) {
        auto t = render_type_at(type_nid);
        if (!t.empty()) {
          print(":");
          print(t);
        }
      }
      // Every `mod` output carries a landing-cycle annotation.  A pipe output
      // (`out:T@[N]`) keeps its declared depth via the trailing stages node;
      // a plain output (slang regs, comb-depth outputs) opts out of the
      // interface-latency assertion with `@[]` (inert — it does not change
      // lowering).
      if (is_mod && is_output) {
        auto st = find_stages_child(port);
        print(std::format("@[{}]", st.is_invalid() ? std::string{} : format_stages(st)));
      }
      first = false;
    }
  }
  print(")");
}

void Lnast_prp_writer::emit_module_header(Lnast_nid io_nid, bool is_mod) {
  auto in_tup  = lnast->get_child(io_nid);
  auto out_tup = in_tup.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(in_tup);
  emit_port_group(in_tup, /*is_output=*/false, is_mod);
  print(" -> ");
  emit_port_group(out_tup, /*is_output=*/true, is_mod);
}

bool Lnast_prp_writer::body_has_state(Lnast_nid nid) const {
  if (nid.is_invalid()) {
    return false;
  }
  // A submodule instantiation lowers to a `func_call`; only a `mod` (or `pipe`)
  // may instantiate (a `comb` calling a sub has "no hardware lowering yet"), so
  // such a module must be emitted as `mod` even when it carries no register.
  if (lnast->get_type(nid) == Lnast_ntype::Lnast_ntype_func_call) {
    return true;
  }
  // A `declare` whose qualifier child (const) is "reg"/"latch" marks state.
  if (lnast->get_type(nid) == Lnast_ntype::Lnast_ntype_declare) {
    // declare( ref, type, const(qualifier), [value] ) — qualifier is child 2.
    auto c0 = lnast->get_child(nid);
    if (!c0.is_invalid()) {
      auto c1 = lnast->get_sibling_next(c0);
      if (!c1.is_invalid()) {
        auto c2 = lnast->get_sibling_next(c1);
        if (!c2.is_invalid() && lnast->get_type(c2) == Lnast_ntype::Lnast_ntype_const) {
          auto q = lnast->get_name(c2);
          if (q == "reg" || q == "latch") {
            return true;
          }
        }
      }
    }
  }
  for (auto c = lnast->get_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (body_has_state(c)) {
      return true;
    }
  }
  return false;
}

std::string Lnast_prp_writer::lambda_name() const {
  std::string_view full = lnast->get_top_module_name();
  auto             dot  = full.rfind('.');
  std::string_view tail = (dot == std::string_view::npos) ? full : full.substr(dot + 1);
  if (tail.empty()) {
    tail = full;
  }
  return std::string(tail);
}

std::string Lnast_prp_writer::render_attr_value(Lnast_nid value_nid) const {
  if (value_nid.is_invalid()) {
    return "true";  // a flag-only attr_set (no value child) reads as true
  }
  auto name = lnast->get_name(value_nid);
  if (lnast->get_type(value_nid) == Lnast_ntype::Lnast_ntype_ref) {
    return std::string(strip_prefix(name));  // e.g. reset_pin=<wire>
  }
  return std::string(name);  // const: number / true / false — verbatim
}

// Collect the slang reader's per-reg/per-memory `attr_set` statements into
// folded_attrs_, mapping the importer attr vocabulary to Pyrope source names:
//   initial=N  -> init=N
//   sync=B     -> async=(!B)   (the importer's `sync` is the inverse of the
//                               source `async`; `sync=false` is an async reset)
//   everything else (reset_pin, negreset, clock_pin, posclk, fwd, …) verbatim.
void Lnast_prp_writer::collect_folded_attrs(Lnast_nid stmts_nid) {
  if (stmts_nid.is_invalid()) {
    return;
  }
  for (auto s = lnast->get_child(stmts_nid); !s.is_invalid(); s = lnast->get_sibling_next(s)) {
    // Recurse into nested blocks (always-block stmts AND if/else arms) so a
    // memory's static config attr written deep inside the body — e.g.
    // mem.[wensize]=N at each write site under `if (ce) if (we) …` — still folds
    // into the declaration.  These attrs carry a constant value (the same in
    // every arm); write_attr_set drops every occurrence via folded_keys_.
    auto st = lnast->get_type(s);
    if (st == Lnast_ntype::Lnast_ntype_stmts || Lnast_ntype::is_if_like(st)) {
      collect_folded_attrs(s);
      continue;
    }
    if (lnast->get_type(s) != Lnast_ntype::Lnast_ntype_attr_set) {
      continue;
    }
    auto var_nid = lnast->get_child(s);
    if (var_nid.is_invalid()) {
      continue;
    }
    auto key_nid = lnast->get_sibling_next(var_nid);
    if (key_nid.is_invalid()) {
      continue;
    }
    // The pyrope-origin decl-class attr (`attr_set x type mut/reg/…`) is handled
    // by write_attr_set/pending_decl_, not folded — leave it alone.
    auto key = std::string(lnast->get_name(key_nid));
    if (key == "type" || key == "comptime") {
      continue;
    }
    auto        val_nid = lnast->get_sibling_next(key_nid);
    std::string val     = render_attr_value(val_nid);

    // A clock/reset PIN attribute binds the flop to a NET (a derived clock such
    // as `gclk = clk_b & gate`), so it must be written `clock_pin=ref <net>` —
    // a bare `clock_pin=<net>` resolves to the net's VALUE at the declare point
    // (the hoisted `0`), which tolg rejects ("names clock_pin '0'").  Record the
    // net so write_module can emit its driver ahead of the reg declare.
    const bool val_is_ref = !val_nid.is_invalid() && lnast->get_type(val_nid) == Lnast_ntype::Lnast_ntype_ref;
    if (val_is_ref && (key == "clock_pin" || key == "reset_pin" || key.ends_with("_pin"))) {
      pin_dep_nets_.insert(val);
      val = "ref " + val;
    }

    auto var0 = std::string(strip_prefix(lnast->get_name(var_nid)));
    folded_keys_.insert(var0 + "\x01" + key);  // record (var,orig-key) for write_attr_set skip

    if (key == "initial") {
      key = "init";
    } else if (key == "sync") {
      key = "async";
      val = (val == "false" || val == "0") ? "true" : "false";
    }

    auto        var = std::string(strip_prefix(lnast->get_name(var_nid)));
    std::string tok = key + "=" + val;
    auto        it  = folded_attrs_.find(var);
    if (it == folded_attrs_.end()) {
      folded_attrs_[var] = tok;
    } else {
      it->second += ", " + tok;
    }
  }
}

void Lnast_prp_writer::collect_driver_reads(Lnast_nid def_node, std::unordered_set<std::string>& out) const {
  auto c0 = lnast->get_child(def_node);
  if (c0.is_invalid()) {
    return;
  }
  // Recurse operands; an inlined single-use temp is replaced by the operands of
  // ITS definition (so `gclk = clk_b & inv` whose `&` rides a folded temp still
  // reports the real read `inv`).  Folds are acyclic (def precedes use), so this
  // terminates.
  auto rec = [&](auto&& self, Lnast_nid node) -> void {
    if (Lnast_ntype::is_ref(lnast->get_type(node))) {
      std::string raw(lnast->get_name(node));
      auto        fit = fold_info_.find(raw);
      if (foldable_.count(raw) && fit != fold_info_.end()) {
        auto d0 = lnast->get_child(fit->second.def_node);
        for (auto c = d0.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(d0); !c.is_invalid();
             c      = lnast->get_sibling_next(c)) {
          self(self, c);
        }
      } else {
        out.insert(std::string(strip_prefix(raw)));
      }
      return;
    }
    for (auto c = lnast->get_child(node); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      self(self, c);
    }
  };
  for (auto c = lnast->get_sibling_next(c0); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    rec(rec, c);
  }
}

void Lnast_prp_writer::write_stmts() {
  // A `stmts` whose parent is itself a `stmts` is a bare lexical block — it is
  // what a constant-folded `if true { … }` collapses to (see the runner splice)
  // or any `{ … }` scope.  It MUST be wrapped in braces: flattening it would
  // merge its declarations into the enclosing scope and trip Pyrope's
  // no-shadowing / no-redeclaration rule (e.g. an arm-local `mut d` colliding
  // with a later top-level `mut d`).  The file-level stmts (parent == top) and
  // if/loop bodies (the if/loop writer emits their own braces) are NOT wrapped.
  const bool scoped = !nid_stack.empty() && lnast->get_type(nid_stack.top()) == Lnast_ntype::Lnast_ntype_stmts;
  if (scoped) {
    print("{\n");
    ++depth;
  }
  if (move_to_child()) {
    do {
      if (is_folded_node(cur) || emits_nothing_stmt(cur)) {
        continue;  // a temp def inlined at its single use, or a folded type_spec/stage decl
      }
      print_indent();
      write_node();
      os << "\n";
    } while (move_to_sibling());
    move_to_parent();
  }
  if (scoped) {
    --depth;
    print_indent();
    print("}");
  }
}

// ── if ────────────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_if() {
  const bool unique = Lnast_ntype::is_unique_if(current_ntype());
  if (!move_to_child()) {
    return;
  }

  // First child: condition (ref or const) — inline a single-use temp condition.
  print(unique ? "unique if " : "if ");
  print(render_value(cur, /*operand_ctx=*/false));
  print(" {\n");
  ++depth;

  // Second child: then-stmts
  if (!move_to_sibling()) {
    --depth;
    println("}");
    move_to_parent();
    return;
  }
  write_node();
  --depth;
  print_indent();
  print("}");

  // Optional: else / elif chains
  while (move_to_sibling()) {
    if (is_last_child()) {
      // Bare else-stmts
      print(" else {\n");
      ++depth;
      write_node();
      --depth;
      print_indent();
      print("}");
    } else {
      // elif condition — inline a single-use temp condition.
      print(" elif ");
      print(render_value(cur, /*operand_ctx=*/false));
      print(" {\n");
      ++depth;
      if (!move_to_sibling()) {
        --depth;
        print_indent();
        print("}");
        break;
      }
      write_node();
      --depth;
      print_indent();
      print("}");
    }
  }

  move_to_parent();
}

// ── declare ─────────────────────────────────────────────────────────────────

// `declare( ref(var), type_decl, const(qualifier), [value] )`.
// Emitted as a standalone Pyrope declaration `<qualifier> var[:type][ = value]`.
// In post-uPass LNAST the initial value is a SEPARATE `store` statement, so most
// declares carry no value child; the following `store(var, value)` then prints a
// plain `var = value` re-bind of the just-declared mut/const variable.  Emitting
// the declaration on its own line (rather than folding the keyword into the next
// write) is what keeps value-less declares — bare `var x:u8`, fully-folded
// `const z` — present so later reads still resolve.
void Lnast_prp_writer::write_declare() {
  // This var's declaration was hoisted to a `mut X = 0` at the function top
  // (it is written across sibling scopes); drop the in-place nested declare.
  if (auto vc = lnast->get_child(cur); !vc.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(vc))
      && suppress_decl_.count(std::string(strip_prefix(lnast->get_name(vc))))) {
    return;
  }
  // A declare carrying a trailing `stages` node is the `stage[N] x = v`
  // lowering (upass/pipe inserts `declare(x, type, reg, stages(min,max))` with
  // no value child).  Record the depth and suppress the bare declare — the
  // following store to `x` re-attaches it as `stage[N] x = v`.  Without this
  // the stages node was mis-read as the init value (`reg out = 3`) and the
  // pipeline depth was lost.
  if (auto st = find_stages_child(cur); !st.is_invalid()) {
    auto        var_nid = lnast->get_child(cur);
    std::string lhs     = var_nid.is_invalid() ? std::string{} : std::string(strip_prefix(lnast->get_name(var_nid)));
    stage_decls_[lhs]   = format_stages(st);
    declared_.insert(lhs);
    return;
  }
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());  // ref(var)
  declared_.insert(std::string(lhs));       // an explicit declare; later writes skip the `mut`

  std::string type_suffix;
  if (move_to_sibling()) {
    type_suffix = render_type();  // type_decl — read-only, leaves the cursor put
  }

  std::string kw = "mut";  // storage class; default to the permissive `mut`
  if (move_to_sibling()) {
    auto qualifier = current_text();  // const(qualifier): "mut"/"const"/"mut wrap"/…
    if (!qualifier.empty()) {
      kw = std::string(qualifier);
    }
  }

  const bool has_value = move_to_sibling();  // optional inline init

  // A `const 'nil'` value is the slang reader's "no reset / no initializer"
  // sentinel — emit a bare declaration (`reg r:u8`) so tolg gives the flop no
  // reset pin (sync reset is carried by the body mux instead).  `= nil` is not
  // a reparsable initializer for an integer reg.
  const bool nil_value = has_value && current_ntype() == Lnast_ntype::Lnast_ntype_const && current_text() == "nil";

  print(kw);
  print(" ");
  print(lhs);
  if (!type_suffix.empty()) {
    print(":");
    print(type_suffix);
  }
  // Fold any collected reg/memory attributes onto the declaration.  With a type
  // the suffix is `:[…]` (single colon after the type); without one it is the
  // `::[…]` prefix form.
  if (auto it = folded_attrs_.find(std::string(lhs)); it != folded_attrs_.end()) {
    print(type_suffix.empty() ? "::[" : ":[");
    print(it->second);
    print("]");
  }
  if (has_value && !nil_value) {
    print(" = ");
    if (current_ntype() == Lnast_ntype::Lnast_ntype_tuple_add) {
      write_tuple_literal();  // memory init: a bare tuple_add (no LHS child)
    } else {
      print(render_value(cur, /*operand_ctx=*/false));
    }
  } else if (!has_value && kw != "reg" && kw != "latch" && !kw.starts_with("reg ")
             && !(kw == "wire" && wire_stored_.count(std::string(lhs)))) {
    // A combinational var declared without an initializer (e.g. a Verilog
    // `BranchProv x;` wire/var) still needs a value in Pyrope — default to 0
    // (the var is unconditionally assigned before any read).  Regs keep their
    // bare form (no initializer = no reset pin).  A `wire` that already has a
    // store driver is single-driver: a `= 0` here would make it multi-driven, so
    // emit the bare `wire X:T` and let the body store be its sole driver.
    print(" = 0");
  }
  move_to_parent();
}

// File-local: parse a const's text (decimal, or 0x… hex, with optional sign)
// into a signed value.  Returns nullopt on any trailing junk / overflow.
static std::optional<long long> parse_int_const(std::string_view s) {
  if (s.empty()) {
    return std::nullopt;
  }
  try {
    size_t    pos = 0;
    long long v   = std::stoll(std::string(s), &pos, 0);
    if (pos != s.size()) {
      return std::nullopt;
    }
    return v;
  } catch (...) {
    return std::nullopt;
  }
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

// Returns N>0 if `s` equals 2^N - 1 (all N low bits set), else 0.  Handles
// arbitrary-width 0x-hex (the >64-bit data buses firtool emits) and narrow
// decimal.  Width recovery for the uN/sN spellings can't go through int64
// (those overflow past 63 bits → the type was lost as `int`).
static int all_ones_width(std::string_view s) {
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    std::string_view h = s.substr(2);
    size_t           i = 0;
    while (i + 1 < h.size() && h[i] == '0') {  // strip leading zeros
      ++i;
    }
    h = h.substr(i);
    if (h.empty()) {
      return 0;
    }
    int top = hex_digit(h[0]);
    int topbits;
    switch (top) {  // most-significant nibble of a 2^N-1 value
      case 1 : topbits = 1; break;
      case 3 : topbits = 2; break;
      case 7 : topbits = 3; break;
      case 15: topbits = 4; break;
      default: return 0;
    }
    for (size_t k = 1; k < h.size(); ++k) {
      if (hex_digit(h[k]) != 15) {
        return 0;
      }
    }
    return topbits + 4 * static_cast<int>(h.size() - 1);
  }
  auto v = parse_int_const(s);
  if (!v || *v < 0) {
    return 0;
  }
  for (int n = 1; n < 63; ++n) {
    if (*v == ((1LL << n) - 1)) {
      return n;
    }
  }
  return 0;
}

// Returns k>=0 if |s| equals 2^k (a single set bit), else -1.  Used to confirm
// the signed sN range bound min == -2^(N-1) at arbitrary width.
static int pow2_width(std::string_view s) {
  if (!s.empty() && s[0] == '-') {
    s = s.substr(1);
  }
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    std::string_view h = s.substr(2);
    size_t           i = 0;
    while (i + 1 < h.size() && h[i] == '0') {
      ++i;
    }
    h = h.substr(i);
    if (h.empty()) {
      return -1;
    }
    int top = hex_digit(h[0]);
    int toplog;
    switch (top) {
      case 1 : toplog = 0; break;
      case 2 : toplog = 1; break;
      case 4 : toplog = 2; break;
      case 8 : toplog = 3; break;
      default: return -1;
    }
    for (size_t k = 1; k < h.size(); ++k) {
      if (hex_digit(h[k]) != 0) {
        return -1;
      }
    }
    return toplog + 4 * static_cast<int>(h.size() - 1);
  }
  auto v = parse_int_const(s);
  if (!v || *v <= 0) {
    return -1;
  }
  for (int k = 0; k < 63; ++k) {
    if (*v == (1LL << k)) {
      return k;
    }
  }
  return -1;
}

// If `s` is a single contiguous run of set bits [lo..hi] (lo may be > 0),
// returns (lo, hi); else nullopt.  Works at ARBITRARY width via the hex string
// (decimal narrow via int64) — a get_mask packs the selected bits LSB-first, so
// a non-zero-based contiguous mask is `src#[lo..=hi]` (which compacts), NOT
// `src & mask` (which leaves them in place).  The from-0 case (lo==0) is the
// width-truncation mask; lo>0 is a bit-field extract / shifter slice.
static std::optional<std::pair<int, int>> contiguous_run(std::string_view s) {
  std::vector<bool> bits;
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    std::string_view h = s.substr(2);
    for (size_t i = h.size(); i-- > 0;) {  // LSB hex digit first
      int d = hex_digit(h[i]);
      if (d < 0) {
        return std::nullopt;
      }
      for (int b = 0; b < 4; ++b) {
        bits.push_back((d >> b) & 1);
      }
    }
  } else {
    auto v = parse_int_const(s);
    if (!v || *v <= 0) {
      return std::nullopt;
    }
    unsigned long long m = static_cast<unsigned long long>(*v);
    for (int b = 0; b < 64; ++b) {
      bits.push_back((m >> b) & 1ULL);
    }
  }
  int lo = -1;
  int hi = -1;
  for (int i = 0; i < static_cast<int>(bits.size()); ++i) {
    if (bits[i]) {
      if (lo < 0) {
        lo = i;
      }
      hi = i;
    }
  }
  if (lo < 0) {
    return std::nullopt;  // all-zero mask
  }
  for (int i = lo; i <= hi; ++i) {
    if (!bits[i]) {
      return std::nullopt;  // non-contiguous
    }
  }
  return std::make_pair(lo, hi);
}

std::string Lnast_prp_writer::render_type() { return render_type_at(cur); }

std::string Lnast_prp_writer::render_type_at(Lnast_nid type_nid) {
  using N = Lnast_ntype;
  switch (lnast->get_type(type_nid)) {
    case N::Lnast_ntype_prim_type_none  : return {};
    case N::Lnast_ntype_prim_type_bool  : return "bool";
    case N::Lnast_ntype_prim_type_string: return "string";
    case N::Lnast_ntype_prim_type_int   : {
      // prim_type_int( [max], [min] ) — both children optional (absent ⇒ unbounded).
      auto c_max = lnast->get_child(type_nid);
      if (c_max.is_invalid()) {
        return "signed";  // unbounded signed (was `int`)
      }
      auto c_min = lnast->get_sibling_next(c_max);
      if (c_min.is_invalid()) {
        return "signed";  // single-sided bound — no clean uN/sN spelling
      }
      auto max_t = lnast->get_name(c_max);
      auto min_t = lnast->get_name(c_min);
      // Unsigned uN: min == 0, max == 2^N - 1.  Width recovery is arbitrary-
      // precision (firtool data buses are routinely 128/256/512 bits wide — the
      // old int64 path overflowed and silently downgraded them to `int`).
      if (min_t == "0") {
        if (int n = all_ones_width(max_t); n > 0) {
          return "u" + std::to_string(n);
        }
      } else if (!min_t.empty() && min_t[0] == '-') {
        // Signed sN: max == 2^(N-1) - 1 (all-ones width N-1), min == -2^(N-1).
        if (int m = all_ones_width(max_t); m > 0 && pow2_width(min_t) == m) {
          return "s" + std::to_string(m + 1);
        }
      }
      return "signed";  // safe, lossy fallback — `signed` (unbounded) accepts any value
    }
    case N::Lnast_ntype_comp_type_array: {
      // comp_type_array( elem_type, const("[N]") ) -> "[N]elemtype".  The size
      // const already carries its brackets (e.g. "[4]"), so concatenate as-is.
      auto elem = lnast->get_child(type_nid);
      if (elem.is_invalid()) {
        return {};
      }
      auto        size_n = lnast->get_sibling_next(elem);
      std::string sz     = size_n.is_invalid() ? std::string{} : std::string(lnast->get_name(size_n));
      return sz + render_type_at(elem);
    }
    default: return {};  // comp_type_tuple / named-type ref — not yet serialised; drop
  }
}

// ── assign ────────────────────────────────────────────────────────────────────

// `store(var, level0..levelN, value)`. 0 levels → `var = value`
// (the old assign spelling, decl keyword preserved); ≥1 level →
// `var[level0]…[levelN] = value` (the old tuple_set spelling). The level-walk
// loop handles both: with no middle siblings it emits just `var = value`.
void Lnast_prp_writer::write_store() {
  if (!move_to_child()) {
    return;
  }
  auto      lhs   = std::string(strip_prefix(current_text()));
  Lnast_nid first = cur;
  // Fold a redundant self-store `lhs = lhs` (a set_mask in-place collapse aliases
  // its versioned result back to the base, so the reader's store-back becomes a
  // no-op).  Only the simple two-child shape (no index levels) can be one.
  if (move_to_sibling()) {
    if (is_last_child() && lnast->get_type(cur) == Lnast_ntype::Lnast_ntype_ref
        && std::string(strip_prefix(current_text())) == lhs) {
      move_to_parent();
      return;  // emit nothing — caller's print_indent left a blank line, harmless
    }
    cur = first;  // restore for the normal path
  }
  // A store to a stage-declared variable re-attaches the pipeline depth that the
  // suppressed `declare` carried: `stage[N] x = v`.  Only the first store
  // declares the stage (later writes are plain assignments).
  if (auto sit = stage_decls_.find(lhs); sit != stage_decls_.end()) {
    print(std::format("stage[{}] ", sit->second));
    stage_decls_.erase(sit);
    print(lhs);
    while (move_to_sibling() && !is_last_child()) {
      print("[");
      print(render_value(cur, /*operand_ctx=*/false));
      print("]");
    }
    print(" = ");
    print(render_value(cur, /*operand_ctx=*/false));
    move_to_parent();
    return;
  }
  // A scalar store (value is the lone RHS child, no index levels) that makes the
  // first declaration of `lhs` carries any `type_spec`-recorded type onto the
  // declaration: `mut x:T = v`.
  auto       val_nid = lnast->get_sibling_next(first);
  const bool scalar  = !val_nid.is_invalid() && lnast->is_last_child(val_nid);
  std::string prefix = decl_prefix(lhs);
  print(prefix);
  print(lhs);
  if (scalar && !prefix.empty()) {
    if (auto tit = type_specs_.find(lhs); tit != type_specs_.end() && !tit->second.empty()) {
      print(":");
      print(tit->second);
    }
  }
  while (move_to_sibling() && !is_last_child()) {
    print("[");
    print(render_value(cur, /*operand_ctx=*/false));
    print("]");
  }
  print(" = ");
  print(render_value(cur, /*operand_ctx=*/false));  // cursor sits on the value (last child)
  move_to_parent();
}

// ── ref / const ───────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_ref() { print(strip_prefix(current_text())); }

// File-local helper: escapes special characters inside a Pyrope string literal.
static std::string escape_string(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"' : out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7f) {
          out += std::format("\\x{:02x}", static_cast<unsigned>(c));
        } else {
          out += static_cast<char>(c);
        }
        break;
    }
  }
  return out;
}

void Lnast_prp_writer::write_const() {
  auto text = current_text();
  if (!text.empty() && (isdigit(static_cast<unsigned char>(text[0])) || text[0] == '-')) {
    print(text);
  } else if (text == "true" || text == "false" || text == "nil") {
    print(text);
  } else {
    os << std::format("\"{}\"", escape_string(text));
  }
}

// ── cassert ───────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_cassert() {
  if (!move_to_child()) {
    return;
  }
  // The new Pyrope grammar requires parens on every call, including
  // `cassert`. Emit `cassert(<cond>)` so the round-trip output reparses
  // under the current grammar.
  print("cassert(");
  print(render_value(cur, /*operand_ctx=*/false));
  // Optional 2nd child: a comptime message string (cassert(cond, "msg")).
  if (move_to_sibling()) {
    print(", ");
    print(render_value(cur, /*operand_ctx=*/false));
  }
  print(")");
  move_to_parent();
}

// ── func_call ─────────────────────────────────────────────────────────────────

// The mod-vs-comb predicate: a subtree carries module state if it holds a
// `reg`/`latch` declare or a submodule `func_call` (a nested instantiation makes
// the enclosing unit a `mod`).  Used by scan_multi_out_comb (destructure
// decision).
bool Lnast_prp_writer::subtree_has_state(const std::shared_ptr<Lnast>& ln, Lnast_nid nid) {
  using N = Lnast_ntype;
  if (nid.is_invalid()) {
    return false;
  }
  if (ln->get_type(nid) == N::Lnast_ntype_func_call) {
    return true;
  }
  if (ln->get_type(nid) == N::Lnast_ntype_declare) {
    auto c0 = ln->get_child(nid);
    auto c1 = c0.is_invalid() ? c0 : ln->get_sibling_next(c0);
    auto c2 = c1.is_invalid() ? c1 : ln->get_sibling_next(c1);
    if (!c2.is_invalid() && ln->get_type(c2) == N::Lnast_ntype_const) {
      auto q = ln->get_name(c2);
      if (q == "reg" || q == "latch") {
        return true;
      }
    }
  }
  for (auto c = ln->get_child(nid); !c.is_invalid(); c = ln->get_sibling_next(c)) {
    if (subtree_has_state(ln, c)) {
      return true;
    }
  }
  return false;
}

// Scan a unit's top to detect a slang-origin multi-output `comb` (see the header
// note).  Mirrors write_module's `is_mod = body_has_state(body)` decision and
// emit_port_group's output-port walk, but as a pure read over an arbitrary unit.
bool Lnast_prp_writer::scan_multi_out_comb(const std::shared_ptr<Lnast>& ln, std::string& name,
                                           std::vector<std::string>& outputs) {
  using N   = Lnast_ntype;
  auto root = ln->get_root();
  if (root.is_invalid()) {
    return false;
  }
  auto io = ln->get_child(root);
  if (io.is_invalid() || ln->get_type(io) != N::Lnast_ntype_io) {
    return false;  // pyrope-origin bare file (func_defs), not a slang io module
  }
  auto body = ln->get_sibling_next(io);
  // `is_mod`: a body with state (reg/latch) OR a submodule func_call must be a
  // `mod`; only a stateless, instantiation-free body is an inlined `comb`.
  if (subtree_has_state(ln, body)) {
    return false;  // a mod: stays a Sub instance, no destructure needed
  }
  // Output port names: the io node's SECOND tuple_add child; each port is
  // `store(ref(name), …)`.
  auto in_tup  = ln->get_child(io);
  auto out_tup = in_tup.is_invalid() ? Lnast_nid{} : ln->get_sibling_next(in_tup);
  if (out_tup.is_invalid()) {
    return false;
  }
  for (auto port = ln->get_child(out_tup); !port.is_invalid(); port = ln->get_sibling_next(port)) {
    auto name_nid = ln->get_child(port);
    if (name_nid.is_invalid()) {
      continue;
    }
    auto pn = ln->get_name(name_nid);
    if (!pn.empty() && pn[0] == '\\') {
      pn = pn.substr(1);  // strip the SSA/escape prefix the same way strip_prefix does for refs
    }
    outputs.emplace_back(pn);
  }
  if (outputs.size() <= 1) {
    return false;  // single-output comb binds to one var fine — no destructure
  }
  std::string_view full = ln->get_top_module_name();
  auto             dot  = full.rfind('.');
  name                  = std::string(dot == std::string_view::npos ? full : full.substr(dot + 1));
  return true;
}

void Lnast_prp_writer::write_func_call() {
  if (!move_to_child()) {
    return;
  }
  // LHS (the result/instance variable)
  auto lhs = std::string(strip_prefix(current_text()));
  // function name (next sibling)
  move_to_sibling();
  std::string call_name(current_text());
  std::string call_tail = call_name;
  if (auto p = call_tail.rfind('.'); p != std::string::npos) {
    call_tail = call_tail.substr(p + 1);
  }

  // A multi-output COMB bound to a real var is rejected by the runner as
  // `multi-output-one-var` (combs are inlined, so the whole-bind keeps all
  // returns).  Emit a destructure instead — `mut (r__o0 = C.o0, …) = C(args)` —
  // and record r__o<i> so the body's `r["o<i>"]` tuple_gets rewrite to it.
  const std::vector<std::string>* outs = nullptr;
  if (multi_out_combs_ && !is_tmp(lhs)) {
    if (auto it = multi_out_combs_->find(call_tail); it != multi_out_combs_->end()) {
      outs = &it->second;
    }
  }

  // Preserve the hierarchical instance name.  A call to an emitted module becomes
  // a Sub instance on re-compile (every module with `upass.inline=false`; a
  // stateful `mod` always); without an explicit name tolg synthesises
  // `u_<callee>_<tmp>` (the bound var is a temp in the parsed LNAST), losing
  // correspondence with the original v2prp source hierarchy.  Annotate
  // `Callee::[name=<lhs>]` so the Sub takes the bound variable's name.  Emitted
  // for any real (non-temp) LHS bound to a known module — stateless `comb`s
  // included: when such a comb is actually inlined the runner consumes the name
  // as the inline hierarchy level (identical to the dst-name fallback), so it is
  // never harmful.
  const bool name_instance = !is_tmp(lhs) && instantiated_modules_ != nullptr
                             && instantiated_modules_->count(call_tail) != 0u;

  if (outs != nullptr) {
    mocomb_dst_.insert(lhs);
    print("mut (");
    bool firstd = true;
    for (const auto& o : *outs) {
      if (!firstd) {
        print(", ");
      }
      std::string tgt = lhs + "__" + o;
      mocomb_field_[lhs + std::string(1, '\x01') + o] = tgt;
      print(tgt);
      print(" = ");
      print(call_name);
      print(".");
      print(o);
      firstd = false;
    }
    print(") = ");
    print(call_name);
  } else {
    print(decl_prefix(lhs));
    print(lhs);
    print(" = ");
    print(call_name);
  }
  if (name_instance) {
    print("::[name=");
    print(lhs);
    print("]");
  }
  print("(");
  // arguments — positional args are ref/const nodes; named args are assign
  // nodes (name = value) that must NOT carry the "mut" keyword in call context.
  bool first = true;
  while (move_to_sibling()) {
    if (!first) {
      print(", ");
    }
    if (current_ntype() == Lnast_ntype::Lnast_ntype_store) {
      // Named actual: emit as  name = value  (no "mut").
      if (move_to_child()) {
        print(strip_prefix(current_text()));  // argument name
        print(" = ");
        if (move_to_sibling()) {
          print(render_value(cur, /*operand_ctx=*/false));  // argument value
        }
        move_to_parent();
      }
    } else {
      print(render_value(cur, /*operand_ctx=*/false));
    }
    first = false;
  }
  print(")");
  move_to_parent();
}

// ── func_def ──────────────────────────────────────────────────────────────────

// func_def( ref(name), const(kind), tuple_add(generics), tuple_add(inputs),
//           tuple_add(outputs), stmts(body) ) — a pyrope-origin lambda the
// runner did not flatten into the io+body form (a nested helper, or a top-level
// lambda emitted through the minimal `noop` path).  Re-emit it as
// `kind name[<generics>](in:T,…) -> (out:T,…) { body }`, reusing the shared
// port-group/body machinery.  The top module is still emitted via write_module
// (the slang io node); this handles the remaining func_def statements.
void Lnast_prp_writer::write_func_def() {
  Lnast_nid fd = cur;

  auto name_nid = lnast->get_child(fd);
  if (name_nid.is_invalid()) {
    return;
  }
  auto kind_nid = lnast->get_sibling_next(name_nid);
  auto gen_nid  = kind_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(kind_nid);
  auto in_nid   = gen_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(gen_nid);
  auto out_nid  = in_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(in_nid);
  auto body_nid = out_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(out_nid);

  std::string kind   = kind_nid.is_invalid() ? std::string("comb") : std::string(lnast->get_name(kind_nid));
  std::string name   = std::string(strip_prefix(lnast->get_name(name_nid)));
  const bool  is_mod = (kind == "mod" || kind == "pipe");

  print(kind);
  print(" ");
  print(name);
  // Generic template parameters `<T, U>` (each a ref child of the generics
  // tuple_add); absent when the tuple has no children.
  if (!gen_nid.is_invalid() && !lnast->get_child(gen_nid).is_invalid()) {
    print("<");
    bool gfirst = true;
    for (auto g = lnast->get_child(gen_nid); !g.is_invalid(); g = lnast->get_sibling_next(g)) {
      if (!gfirst) {
        print(", ");
      }
      print(strip_prefix(lnast->get_name(g)));
      gfirst = false;
    }
    print(">");
  }
  emit_port_group(in_nid, /*is_output=*/false, is_mod);
  print(" -> ");
  emit_port_group(out_nid, /*is_output=*/true, is_mod);
  print(" {\n");
  ++depth;
  if (!body_nid.is_invalid()) {
    // Push the func_def so write_stmts sees a non-`stmts` parent (no extra
    // braces — we just opened them); balanced by the pop below.
    nid_stack.push(fd);
    cur = body_nid;
    write_node();  // body stmts
    cur = nid_stack.top();
    nid_stack.pop();
  }
  --depth;
  print_indent();
  print("}");
  cur = fd;  // restore for the caller's move_to_sibling()
}

// ── for ───────────────────────────────────────────────────────────────────────

// for( value_ref, iterable_ref, stmts(body), const(mode) [, idx_ref [, key_ref]] )
// The metadata (value/iter/mode/idx/key) is read via direct tree accessors —
// `mode` sits AFTER the body, so a pure left-to-right cursor walk could not emit
// the `for … in …` header before the braced body.  The body is then emitted
// through the shared cursor so nested folding/indentation behave exactly as in
// any other block.
void Lnast_prp_writer::write_for() {
  Lnast_nid forn = cur;

  auto value_nid = lnast->get_child(forn);
  if (value_nid.is_invalid()) {
    return;
  }
  auto iter_nid = lnast->get_sibling_next(value_nid);
  auto body_nid = iter_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(iter_nid);
  auto mode_nid = body_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(body_nid);

  // Optional (idx[, key]) position/key bindings of `for (v, idx, key) in t`.
  std::vector<std::string> extra;
  for (auto e = mode_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(mode_nid); !e.is_invalid();
       e      = lnast->get_sibling_next(e)) {
    extra.emplace_back(strip_prefix(lnast->get_name(e)));
  }

  std::string value = std::string(strip_prefix(lnast->get_name(value_nid)));
  std::string iter  = iter_nid.is_invalid() ? std::string{} : std::string(strip_prefix(lnast->get_name(iter_nid)));
  std::string mode  = mode_nid.is_invalid() ? std::string{} : std::string(lnast->get_name(mode_nid));

  // Pyrope binds the INDEX first: `for (index, value [, key]) in t` (prp2lnast
  // sets value_ref = bind_refs[1], idx_ref = bind_refs[0]).  The LNAST for-node
  // stores value at child0 and the trailing idx/key after `mode`, so re-emit as
  // (idx, value[, key]) — emitting (value, idx) would swap their roles on
  // re-parse (the index would be read as the value).
  std::string binds = value;
  if (!extra.empty()) {
    binds = "(" + extra[0] + ", " + value;  // extra[0] = idx_ref
    if (extra.size() > 1) {
      binds += ", " + extra[1];  // extra[1] = key_ref
    }
    binds += ")";
  }

  print("for ");
  print(binds);
  print(" in ");
  if (mode == "ref") {
    print("ref ");  // mutable-element iteration: writes are reflected back into the source tuple
  }
  print(iter);
  print(" {\n");
  ++depth;
  if (!body_nid.is_invalid()) {
    // Push the `for` node so write_stmts sees a non-`stmts` parent and does NOT
    // add its own braces (we just opened them); it still folds/indents each
    // body statement.  The push is balanced by the pop below.
    nid_stack.push(forn);
    cur = body_nid;
    write_node();  // body stmts
    cur = nid_stack.top();
    nid_stack.pop();
  }
  --depth;
  print_indent();
  print("}");
  cur = forn;  // restore for the caller's move_to_sibling()
}

// ── Tuples ────────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_tuple_add() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  print(decl_prefix(lhs));
  print(lhs);
  print(" = (");
  bool first = true;
  while (move_to_sibling()) {
    if (!first) {
      print(", ");
    }
    // A `store` element is a NAMED field (`name = value`) — emit it through
    // write_store so the field name (e.g. the memory config's `bits`) survives;
    // a positional element is a plain value that may inline a single-use temp.
    if (current_ntype() == Lnast_ntype::Lnast_ntype_store) {
      write_node();
    } else {
      print(render_value(cur, /*operand_ctx=*/false));
    }
    first = false;
  }
  print(")");
  move_to_parent();
}

void Lnast_prp_writer::write_tuple_concat() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());  // dst
  print(decl_prefix(lhs));
  print(lhs);
  print(" = (");
  // Each remaining child is a source tuple, splatted via the spread operator
  // so the concatenation flattens into one literal (`(...a, ...b)`).
  bool first = true;
  while (move_to_sibling()) {
    if (!first) {
      print(", ");
    }
    print("...");
    print(render_value(cur, /*operand_ctx=*/false));
    first = false;
  }
  print(")");
  move_to_parent();
}

void Lnast_prp_writer::write_tuple_literal() {
  // tuple_add( v0, v1, … ) used as a value (no LHS child) -> `(v0, v1, …)`.
  print("(");
  if (move_to_child()) {
    bool first = true;
    do {
      if (!first) {
        print(", ");
      }
      if (current_ntype() == Lnast_ntype::Lnast_ntype_store) {
        write_node();  // named field `name = value`
      } else {
        print(render_value(cur, /*operand_ctx=*/false));
      }
      first = false;
    } while (move_to_sibling());
    move_to_parent();
  }
  print(")");
}

// ── Attributes ────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_attr_set() {
  if (!move_to_child()) {
    return;
  }
  auto var_name = strip_prefix(current_text());

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  // Suppress LNAST-internal type annotations: attr_set x type mut/reg/wire.
  // Record the storage-class keyword in pending_decl_ so the NEXT assignment
  // to this variable can emit "mut x = …" exactly once.
  if (current_text() == "type") {
    if (move_to_sibling()) {
      auto kw = current_text();
      if (kw == "mut" || kw == "reg" || kw == "wire") {
        pending_decl_[std::string(var_name)] = std::string(kw);
      }
    }
    move_to_parent();
    return;
  }

  // A folded attr (collected into a declaration's `:[…]` suffix) must NOT also
  // be emitted as a standalone statement (it would be an assignment to an
  // undeclared `var.[attr]`).  This catches occurrences deeper than the
  // top-level body (e.g. mem.[wensize]=N inside the always block).
  if (folded_keys_.count(std::string(var_name) + "\x01" + std::string(current_text()))) {
    move_to_parent();
    return;
  }

  // Generic attribute set/flag: `var.[attr] = value` (or `var.[attr]` when the
  // attr_set carries no value child).  The attr name is a bare identifier, so
  // print the const text directly rather than through write_const (which would
  // quote it as a string literal).
  print(var_name);
  print(".[");
  print(current_text());  // attr name (cursor is on the attr const)
  print("]");
  if (move_to_sibling()) {
    print(" = ");
    print(render_value(cur, /*operand_ctx=*/false));  // value
  }

  move_to_parent();
}

// ── delay_assign ──────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_delay_assign() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  take_decl_keyword(lhs);  // consume any pending decl so it doesn't leak
  print(lhs);
  print(" = #[");
  move_to_sibling();
  print(render_value(cur, /*operand_ctx=*/false));
  if (move_to_sibling()) {
    print(", ");
    print(render_value(cur, /*operand_ctx=*/false));
  }
  print("]");
  move_to_parent();
}

// Decompose a (hex- or decimal-) constant mask into its maximal contiguous runs
// of set bits, LSB-first.  Empty when the mask is zero/unparsable.  Each run is
// a closed `[lo..hi]` bit range.  set_mask places the inserted value LSB-first
// across all selected bits, so run k consumes the next (hi-lo+1) bits of the
// insert value after the runs below it.
static std::vector<std::pair<int, int>> mask_runs(std::string_view s) {
  std::vector<bool> bits;
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    std::string_view h = s.substr(2);
    for (size_t i = h.size(); i-- > 0;) {  // LSB hex digit first
      int d = hex_digit(h[i]);
      if (d < 0) {
        return {};
      }
      for (int b = 0; b < 4; ++b) {
        bits.push_back((d >> b) & 1);
      }
    }
  } else {
    auto v = parse_int_const(s);
    if (!v || *v <= 0) {
      return {};
    }
    unsigned long long m = static_cast<unsigned long long>(*v);
    for (int b = 0; b < 64; ++b) {
      bits.push_back((m >> b) & 1ULL);
    }
  }
  std::vector<std::pair<int, int>> runs;
  int                              lo = -1;
  for (int i = 0; i < static_cast<int>(bits.size()); ++i) {
    if (bits[i]) {
      if (lo < 0) {
        lo = i;
      }
    } else if (lo >= 0) {
      runs.emplace_back(lo, i - 1);
      lo = -1;
    }
  }
  if (lo >= 0) {
    runs.emplace_back(lo, static_cast<int>(bits.size()) - 1);
  }
  return runs;
}

// set_mask( dst, val, mask, ins ) — dst = val with the bits selected by the
// constant `mask` replaced by `ins` (placed LSB-first across the selected bits).
// Reparsable spelling: a bit-range LHS assign `dst#[lo..=hi] = ins`, which
// prp2lnast re-lowers to exactly this set_mask shape (read-modify-write).  The
// slang reader emits dst==val (in-place RMW); when they differ (e.g. prp2lnast
// minted a fresh result temp) a `dst = val` base copy is emitted first.  A
// non-contiguous mask is split into one bit-range assign per contiguous run,
// each consuming the next slice of `ins` (LSB-first), so scattered set_masks
// stay correct rather than dropping logic.
void Lnast_prp_writer::write_set_mask() {
  if (!move_to_child()) {
    return;
  }
  std::string dst = std::string(strip_prefix(current_text()));  // SSA suffix stripped
  std::string val = dst;
  if (move_to_sibling()) {  // val (base) — may be a single-use temp to inline
    val = render_value(cur, /*operand_ctx=*/true);
  }
  std::string mask_txt;
  if (move_to_sibling()) {  // mask const
    mask_txt = std::string(current_text());
  }
  std::string ins;
  if (move_to_sibling()) {  // insert value — may be a single-use temp to inline
    ins = render_value(cur, /*operand_ctx=*/true);
  }
  move_to_parent();

  // A set_mask is an in-place RMW.  After SSA stripping, the slang reader's
  // versioned result (`set_mask(OUT___ssa_1, OUT, ..)`) collapses to dst==val,
  // i.e. an in-place write on the base (the redundant `OUT = OUT___ssa_1`
  // store-back then folds away in write_store).  When the base genuinely differs
  // from the source value, copy it in first.
  std::string target   = dst;
  bool        need_sep = false;
  if (dst != val) {
    print(decl_prefix(target));
    print(target);
    os << std::format(" = {}", val);
    need_sep = true;
  }

  auto runs = mask_runs(mask_txt);
  if (runs.empty()) {
    // Zero / unparsable mask: nothing to overwrite.  Emit a base copy if we
    // haven't already (keeps the statement non-empty and the value flowing).
    if (!need_sep) {
      os << std::format("{} = {}", target, val);
    }
    return;
  }

  int ins_off = 0;  // LSB-first cursor into the insert value across runs
  for (auto [lo, hi] : runs) {
    if (need_sep) {
      os << "\n";
      print_indent();
    }
    int w = hi - lo + 1;
    if (ins_off == 0 && runs.size() == 1) {
      // Single run from bit 0 of `ins`: the slice width truncates `ins` itself.
      os << std::format("{}#[{}..={}] = {}", target, lo, hi, ins);
    } else {
      os << std::format("{}#[{}..={}] = {}#[{}..={}]", target, lo, hi, ins, ins_off, ins_off + w - 1);
    }
    ins_off  += w;
    need_sep  = true;
  }
}

// ── Single-use temp folding ─────────────────────────────────────────────────

bool Lnast_prp_writer::defines_child0(Lnast_ntype::Lnast_ntype_int t) {
  using N = Lnast_ntype;
  if (!infix_symbol(t).empty()) {
    return true;
  }
  switch (t) {
    case N::Lnast_ntype_log_not:
    case N::Lnast_ntype_bit_not:
    case N::Lnast_ntype_red_or:
    case N::Lnast_ntype_red_and:
    case N::Lnast_ntype_red_xor:
    case N::Lnast_ntype_popcount:
    case N::Lnast_ntype_sext:
    case N::Lnast_ntype_set_mask:
    case N::Lnast_ntype_get_mask:
    case N::Lnast_ntype_store:
    case N::Lnast_ntype_declare:
    case N::Lnast_ntype_dp_assign:
    case N::Lnast_ntype_delay_assign:
    case N::Lnast_ntype_range:
    case N::Lnast_ntype_tuple_add:
    case N::Lnast_ntype_tuple_concat:
    case N::Lnast_ntype_tuple_get:
    case N::Lnast_ntype_attr_set:
    case N::Lnast_ntype_attr_get:
    case N::Lnast_ntype_func_call   : return true;
    // if/unique_if/cassert/for/while and the pseudo-func_* nodes read child0 (a
    // condition / value), so leave it classified as a USE — the safe default
    // (over-counting a use only blocks a fold; mis-marking a use as a def could
    // wrongly inline a multiply-read temp).
    default                         : return false;
  }
}

bool Lnast_prp_writer::is_pure_copy(Lnast_nid store_node) const {
  auto c0 = lnast->get_child(store_node);
  if (c0.is_invalid()) {
    return false;
  }
  auto val = lnast->get_sibling_next(c0);
  if (val.is_invalid() || !lnast->get_sibling_next(val).is_invalid()) {
    return false;  // value-less, or has index levels (a field write, not a copy)
  }
  auto vt = lnast->get_type(val);
  return vt == Lnast_ntype::Lnast_ntype_ref || vt == Lnast_ntype::Lnast_ntype_const;
}

bool Lnast_prp_writer::operands_stable(Lnast_nid def_node, int d, int u) const {
  int pos = 0;
  for (auto c = lnast->get_child(def_node); !c.is_invalid(); c = lnast->get_sibling_next(c), ++pos) {
    if (pos == 0) {
      continue;  // the LHS being defined
    }
    if (lnast->get_type(c) != Lnast_ntype::Lnast_ntype_ref) {
      continue;  // const / type leaf — never changes
    }
    auto it = write_idx_.find(std::string(lnast->get_name(c)));
    if (it == write_idx_.end()) {
      continue;  // never assigned (io input / const-fed) — stable
    }
    for (int w : it->second) {
      if (w > d && w < u) {
        return false;  // operand reassigned between the def and its single use
      }
    }
  }
  return true;
}

void Lnast_prp_writer::scan_node(Lnast_nid nid, int& index) {
  const int  my_index = index++;
  const auto t        = lnast->get_type(nid);
  const bool def0     = defines_child0(t);

  // Record a `type_spec(ref(var), type)` so the variable's first declaration can
  // fold the type in (`mut x:T = v`); the standalone statement emits nothing.
  if (t == Lnast_ntype::Lnast_ntype_type_spec) {
    auto var_nid = lnast->get_child(nid);
    if (!var_nid.is_invalid()) {
      auto type_nid = lnast->get_sibling_next(var_nid);
      if (!type_nid.is_invalid()) {
        type_specs_[std::string(strip_prefix(lnast->get_name(var_nid)))] = render_type_at(type_nid);
      }
    }
  }
  // Record a stage declare (`declare(var, type, reg, stages(min,max))`, the
  // `stage[N] x = v` lowering) so the following store re-attaches the depth as
  // `stage[N] x = v`; the bare declare itself emits nothing (skipped below).
  if (t == Lnast_ntype::Lnast_ntype_declare) {
    if (auto st = find_stages_child(nid); !st.is_invalid()) {
      auto var_nid = lnast->get_child(nid);
      if (!var_nid.is_invalid()) {
        stage_decls_[std::string(strip_prefix(lnast->get_name(var_nid)))] = format_stages(st);
      }
    }
  }

  int pos = 0;
  for (auto c = lnast->get_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c), ++pos) {
    if (lnast->get_type(c) == Lnast_ntype::Lnast_ntype_ref) {
      std::string nm(lnast->get_name(c));
      auto&       fi = fold_info_[nm];
      if (def0 && pos == 0) {
        fi.def_count++;
        fi.def_node  = nid;
        fi.def_type  = t;
        fi.def_index = my_index;
        if (t != Lnast_ntype::Lnast_ntype_declare) {
          write_idx_[nm].push_back(my_index);  // pushed in increasing index order
        }
      } else {
        fi.use_count++;
        fi.use_index = my_index;
        if (my_index < fi.min_use_index) {
          fi.min_use_index = my_index;
        }
      }
    }
    scan_node(c, index);  // pre-order recurse (leaves just advance the counter)
  }
  if (t == Lnast_ntype::Lnast_ntype_get_mask) {
    get_mask_nodes_.push_back(nid);
  }
  if (t == Lnast_ntype::Lnast_ntype_tuple_get) {
    tuple_get_nodes_.emplace_back(nid, my_index);
  }
  if (t == Lnast_ntype::Lnast_ntype_store) {
    store_nodes_.emplace_back(nid, my_index);
  }
  if (t == Lnast_ntype::Lnast_ntype_func_call) {
    auto fc0    = lnast->get_child(nid);                                          // result
    auto callee = fc0.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(fc0);  // callee name
    if (!callee.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(callee))) {
      func_call_callees_.insert(std::string(strip_prefix(lnast->get_name(callee))));
    }
  }
}

void Lnast_prp_writer::analyze_folding() {
  fold_info_.clear();
  func_call_callees_.clear();
  write_idx_.clear();
  foldable_.clear();
  folded_node_.clear();
  range_lohi_.clear();
  get_mask_nodes_.clear();
  tuple_get_nodes_.clear();
  store_nodes_.clear();
  type_specs_.clear();
  stage_decls_.clear();

  int index = 0;
  scan_node(lnast->get_root(), index);

  // A range temp feeding a get_mask mask reconstructs a `src#[lo..=hi]` slice.
  // Record its bounds, and (when the range is used only there) suppress the
  // standalone range statement.
  for (auto gm : get_mask_nodes_) {
    auto src = lnast->get_child(gm);
    if (src.is_invalid()) {
      continue;
    }
    src = lnast->get_sibling_next(src);  // child1: src
    if (src.is_invalid()) {
      continue;
    }
    auto mask = lnast->get_sibling_next(src);  // child2: mask
    if (mask.is_invalid() || lnast->get_type(mask) != Lnast_ntype::Lnast_ntype_ref) {
      continue;
    }
    std::string mn(lnast->get_name(mask));
    auto        it = fold_info_.find(mn);
    if (it == fold_info_.end() || it->second.def_type != Lnast_ntype::Lnast_ntype_range) {
      continue;
    }
    auto rlo = lnast->get_child(it->second.def_node);
    if (rlo.is_invalid()) {
      continue;
    }
    rlo             = lnast->get_sibling_next(rlo);  // child1: lo
    auto        rhi = rlo.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(rlo);
    std::string lo  = rlo.is_invalid() ? std::string("0") : std::string(lnast->get_name(rlo));
    std::string hi  = rhi.is_invalid() ? lo : std::string(lnast->get_name(rhi));
    range_lohi_[mn] = {lo, hi};
    if (it->second.use_count == 1) {
      folded_node_.insert(it->second.def_node.get_class_index().value);  // range stmt inlined into the slice
    }
  }

  // Select the single-def / single-use temps whose value-producing definition
  // can be inlined back into the (one) use.
  for (auto& [name, fi] : fold_info_) {
    if (!is_tmp(name)) {
      continue;  // only `___` compiler temps are inlined
    }
    if (fi.def_count != 1 || fi.use_count != 1) {
      continue;  // must be written once and read once
    }
    if (fi.def_index < 0 || fi.use_index < 0 || fi.def_index >= fi.use_index) {
      continue;  // need a forward def-before-use
    }
    bool ok = is_foldable_optype(fi.def_type);
    if (fi.def_type == Lnast_ntype::Lnast_ntype_store) {
      ok = is_pure_copy(fi.def_node);  // a bare copy `___t = x`
    }
    if (!ok) {
      continue;
    }
    if (!operands_stable(fi.def_node, fi.def_index, fi.use_index)) {
      continue;
    }
    foldable_.insert(name);
    folded_node_.insert(fi.def_node.get_class_index().value);
  }
}

// Inline submodule output-port reads.  A multi-output instance's outputs are
// extracted as `_t = inst["port"]` and read elsewhere by the temp name.  When
// the instance is declared (positionally) before EVERY use of `_t`, drop the
// temp (and its hoisted `wire`) and read `inst.port` directly at each use.  A
// use that PRECEDES the instance declaration is genuine pipeline feedback
// (hazard / forwarding / writeback) — there the temp stays a position-
// independent `wire` driven by `_t = inst.port`.  Must run after the clock/reset
// pin cone is known (a cone net needs a real name and is never inlined).
void Lnast_prp_writer::analyze_instance_inline() {
  using N = Lnast_ntype;
  instance_results_.clear();
  instance_output_inlined_.clear();

  // Names defined by a single module-instance call: their outputs print as
  // `inst.port` (also enables dot rendering for the kept `wire` drivers).
  for (const auto& [name, fi] : fold_info_) {
    if (fi.def_count == 1 && fi.def_type == N::Lnast_ntype_func_call) {
      instance_results_.insert(std::string(strip_prefix(name)));
    }
  }
  if (instance_results_.empty()) {
    return;
  }

  // The instance whose output a tuple_get reads, or invalid if the tuple_get is
  // not a single-port read `___x = inst["port"]` of a known instance result.
  auto instance_of_tuple_get = [&](Lnast_nid tg) -> Lnast_nid {
    auto c0 = lnast->get_child(tg);
    if (c0.is_invalid() || !N::is_ref(lnast->get_type(c0))) {
      return Lnast_nid{};
    }
    auto base = lnast->get_sibling_next(c0);
    if (base.is_invalid() || !N::is_ref(lnast->get_type(base))) {
      return Lnast_nid{};
    }
    auto idx = lnast->get_sibling_next(base);
    if (idx.is_invalid() || !lnast->get_sibling_next(idx).is_invalid() || lnast->get_type(idx) != N::Lnast_ntype_const) {
      return Lnast_nid{};  // need exactly one constant field index (a named output port)
    }
    return instance_results_.count(std::string(strip_prefix(lnast->get_name(base)))) != 0u ? base : Lnast_nid{};
  };

  // Mark the named extraction temp `_t` (defined once, at `def_node`/`def_index`,
  // by a read of `inst`'s output) for inlining as `inst.port` at every use — when
  // every use follows `inst`'s declaration.  A use that precedes it is pipeline
  // feedback, where `_t` stays a position-independent `wire`.
  auto try_inline = [&](Lnast_nid def_node, int def_index, Lnast_nid inst, N::Lnast_ntype_int def_type) {
    auto c0 = lnast->get_child(def_node);  // _t
    if (c0.is_invalid() || !N::is_ref(lnast->get_type(c0))) {
      return;
    }
    std::string base_name(strip_prefix(lnast->get_name(inst)));
    auto        bwit = write_idx_.find(std::string(lnast->get_name(inst)));
    if (bwit == write_idx_.end() || bwit->second.size() != 1) {
      return;  // instance reassigned (or odd) — leave alone
    }
    int inst_def_index = bwit->second.front();

    std::string raw_name(lnast->get_name(c0));
    std::string tname(strip_prefix(raw_name));
    // `_t` is written exactly once — by THIS def (its `wire` declare is not a
    // write, so write_idx_ excludes it).
    auto twit = write_idx_.find(raw_name);
    if (twit == write_idx_.end() || twit->second.size() != 1 || twit->second.front() != def_index) {
      return;
    }
    auto fit = fold_info_.find(raw_name);
    if (fit == fold_info_.end() || fit->second.use_count < 1) {
      return;  // nothing to inline (leave a dead read alone)
    }
    if (pin_cone_.count(tname) != 0u || pin_cone_.count(base_name) != 0u) {
      return;  // a clock/reset-cone net keeps its name (`clock_pin=ref <net>`)
    }
    if (fit->second.min_use_index <= inst_def_index) {
      return;  // a use precedes the instance decl -> genuine feedback, keep wire
    }
    // render_value inlines `fold_info_[name].def_node`; point it at this def (a
    // trailing `wire` declare may otherwise be the recorded def).
    fit->second.def_node = def_node;
    fit->second.def_type = def_type;
    foldable_.insert(raw_name);                              // render_value inlines `inst.port` at each use
    folded_node_.insert(def_node.get_class_index().value);   // skip the extraction statement
    suppress_decl_.insert(tname);                            // drop any in-place declare
    instance_output_inlined_.insert(tname);                  // drop the hoisted `wire`/`mut`
  };

  // Case 1: a direct `_t = inst["port"]` (the tuple_get defines the named temp).
  for (const auto& [tg, tg_index] : tuple_get_nodes_) {
    if (auto inst = instance_of_tuple_get(tg); !inst.is_invalid()) {
      try_inline(tg, tg_index, inst, N::Lnast_ntype_tuple_get);
    }
  }
  // Case 2: `_t = ___tmp` (a pure copy) where `___tmp` folds to `inst["port"]`.
  // The slang reader emits the read into a `___tmp`, then copies it to the named
  // net; the single-use folder inlines `___tmp`, so the copy's RHS reads the
  // instance output.
  for (const auto& [st, st_index] : store_nodes_) {
    auto c0 = lnast->get_child(st);  // _t
    if (c0.is_invalid() || !N::is_ref(lnast->get_type(c0))) {
      continue;
    }
    auto val = lnast->get_sibling_next(c0);  // the copied value
    if (val.is_invalid() || !lnast->get_sibling_next(val).is_invalid() || !N::is_ref(lnast->get_type(val))) {
      continue;  // not a pure single-ref copy
    }
    std::string val_raw(lnast->get_name(val));
    if (foldable_.count(val_raw) == 0u) {
      continue;  // RHS is not an inlined temp
    }
    auto vit = fold_info_.find(val_raw);
    if (vit == fold_info_.end() || vit->second.def_type != N::Lnast_ntype_tuple_get) {
      continue;  // RHS does not fold to a tuple_get
    }
    if (auto inst = instance_of_tuple_get(vit->second.def_node); !inst.is_invalid()) {
      try_inline(st, st_index, inst, N::Lnast_ntype_store);
    }
  }
}

std::string Lnast_prp_writer::const_text(Lnast_nid node) const {
  auto text = lnast->get_name(node);
  if (!text.empty() && (isdigit(static_cast<unsigned char>(text[0])) || text[0] == '-')) {
    return std::string(text);
  }
  if (text == "true" || text == "false" || text == "nil") {
    return std::string(text);
  }
  return std::format("\"{}\"", escape_string(text));
}

std::string Lnast_prp_writer::render_value(Lnast_nid node, bool operand_ctx) {
  auto t = lnast->get_type(node);
  if (t == Lnast_ntype::Lnast_ntype_ref) {
    std::string nm(lnast->get_name(node));
    if (is_foldable(nm)) {
      return render_def_rhs(fold_info_.at(nm).def_node, operand_ctx);
    }
    return std::string(strip_prefix(nm));
  }
  if (t == Lnast_ntype::Lnast_ntype_const) {
    return const_text(node);
  }
  // A non-leaf operand (not produced by the flattened LNAST, but be safe).
  return render_def_rhs(node, operand_ctx);
}

std::string Lnast_prp_writer::render_def_rhs(Lnast_nid def, bool operand_ctx) {
  using N   = Lnast_ntype;
  auto t    = lnast->get_type(def);
  auto c0   = lnast->get_child(def);
  auto wrap = [&](std::string s, bool loose) -> std::string {
    return (operand_ctx && loose) ? "(" + s + ")" : s;  // parens only where precedence needs them
  };

  // Infix arithmetic / bitwise / logical / comparison: `a <op> b [<op> c …]`.
  if (auto sym = infix_symbol(t); !sym.empty()) {
    std::string out;
    bool        first = true;
    for (auto c = lnast->get_sibling_next(c0); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (!first) {
        out += " ";
        out += sym;
        out += " ";
      }
      out   += render_value(c, /*operand_ctx=*/true);
      first  = false;
    }
    return wrap(out, /*loose=*/true);
  }

  switch (t) {
    case N::Lnast_ntype_log_not:
    case N::Lnast_ntype_bit_not: {
      auto        opnd  = lnast->get_sibling_next(c0);
      std::string s     = (t == N::Lnast_ntype_log_not) ? "not " : "~";
      s                += opnd.is_invalid() ? std::string{} : render_value(opnd, /*operand_ctx=*/true);
      return wrap(s, /*loose=*/true);
    }
    case N::Lnast_ntype_sext: {
      auto        src = lnast->get_sibling_next(c0);
      auto        pos = src.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(src);
      std::string s   = src.is_invalid() ? std::string{} : render_value(src, /*operand_ctx=*/true);
      std::string p   = pos.is_invalid() ? std::string("0") : std::string(strip_prefix(lnast->get_name(pos)));
      return std::format("{}#sext[0..={}]", s, p);  // postfix — binds tight, never wrapped
    }
    case N::Lnast_ntype_get_mask: {
      auto        src  = lnast->get_sibling_next(c0);
      auto        mask = src.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(src);
      std::string s    = src.is_invalid() ? std::string{} : render_value(src, /*operand_ctx=*/true);
      if (!mask.is_invalid()) {
        if (lnast->get_type(mask) == N::Lnast_ntype_ref) {
          auto rit = range_lohi_.find(std::string(lnast->get_name(mask)));
          if (rit != range_lohi_.end()) {
            return std::format("{}#[{}..={}]", s, rit->second.first, rit->second.second);  // tight
          }
        } else if (lnast->get_type(mask) == N::Lnast_ntype_const) {
          std::string mt(lnast->get_name(mask));
          if (auto run = contiguous_run(mt)) {
            return std::format("{}#[{}..={}]", s, run->first, run->second);  // tight
          }
          return wrap(std::format("{} & {}", s, mt), /*loose=*/true);
        }
      }
      std::string mv = mask.is_invalid() ? std::string("0") : render_value(mask, /*operand_ctx=*/true);
      return wrap(std::format("{} & {}", s, mv), /*loose=*/true);
    }
    case N::Lnast_ntype_tuple_get: {
      auto base = lnast->get_sibling_next(c0);
      // Multi-output-comb rewrite: `r["o"]` where `r` is a destructured comb
      // result becomes the per-output temp `r__o` (see write_func_call).  Only a
      // single constant field index (a named output) rewrites; any other index
      // (bit-slice / multi-level) falls through to the normal postfix form.
      if (!base.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(base))) {
        std::string bn(strip_prefix(lnast->get_name(base)));
        if (mocomb_dst_.count(bn) != 0u) {
          auto idx = lnast->get_sibling_next(base);
          if (!idx.is_invalid() && lnast->get_sibling_next(idx).is_invalid()
              && lnast->get_type(idx) == N::Lnast_ntype_const) {
            std::string field(lnast->get_name(idx));
            if (field.size() >= 2 && (field.front() == '\'' || field.front() == '"') && field.back() == field.front()) {
              field = field.substr(1, field.size() - 2);
            }
            if (auto it = mocomb_field_.find(bn + std::string(1, '\x01') + field); it != mocomb_field_.end()) {
              return it->second;
            }
          }
        }
      }
      // A submodule output read `inst["port"]` prints as `inst.port` (dot field
      // access) when `inst` is a module-instance result and `port` is a bare
      // identifier.  This covers both the inlined uses and any remaining `wire`
      // driver.  Non-instance tuple reads keep the bracket-string form.
      if (!base.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(base))) {
        std::string bn(strip_prefix(lnast->get_name(base)));
        auto        idx0 = lnast->get_sibling_next(base);
        if (instance_results_.count(bn) != 0u && mocomb_dst_.count(bn) == 0u && !idx0.is_invalid()
            && lnast->get_sibling_next(idx0).is_invalid() && lnast->get_type(idx0) == N::Lnast_ntype_const) {
          std::string field(lnast->get_name(idx0));
          if (field.size() >= 2 && (field.front() == '\'' || field.front() == '"') && field.back() == field.front()) {
            field = field.substr(1, field.size() - 2);
          }
          auto is_ident = [](const std::string& f) {
            if (f.empty() || !((f[0] >= 'a' && f[0] <= 'z') || (f[0] >= 'A' && f[0] <= 'Z') || f[0] == '_')) {
              return false;
            }
            for (char ch : f) {
              if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')) {
                return false;
              }
            }
            return true;
          };
          if (is_ident(field)) {
            return bn + "." + field;  // postfix dot access (binds tight, never wrapped)
          }
        }
      }
      std::string s = base.is_invalid() ? std::string{} : render_value(base, /*operand_ctx=*/true);
      for (auto idx = base.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(base); !idx.is_invalid();
           idx      = lnast->get_sibling_next(idx)) {
        s += "[" + render_value(idx, /*operand_ctx=*/false) + "]";
      }
      return s;  // postfix
    }
    case N::Lnast_ntype_attr_get: {
      auto        base = lnast->get_sibling_next(c0);
      std::string s    = base.is_invalid() ? std::string{} : render_value(base, /*operand_ctx=*/true);
      // Each remaining sibling is an attr name (a bare const) -> `.[name]`.
      for (auto a = base.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(base); !a.is_invalid();
           a      = lnast->get_sibling_next(a)) {
        s += ".[";
        s += lnast->get_name(a);
        s += "]";
      }
      return s;  // postfix
    }
    case N::Lnast_ntype_store: {
      auto val = lnast->get_sibling_next(c0);  // a pure copy: value is the lone RHS child
      return val.is_invalid() ? std::string{} : render_value(val, operand_ctx);
    }
    case N::Lnast_ntype_range: {
      auto        lo  = lnast->get_sibling_next(c0);
      auto        hi  = lo.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(lo);
      std::string los = lo.is_invalid() ? std::string("0") : std::string(strip_prefix(lnast->get_name(lo)));
      std::string his = hi.is_invalid() ? los : std::string(strip_prefix(lnast->get_name(hi)));
      return std::format("{}..={}", los, his);
    }
    default:
      // Not an inline-able value op (reached only defensively).
      return c0.is_invalid() ? std::string{} : std::string(strip_prefix(lnast->get_name(c0)));
  }
}

void Lnast_prp_writer::write_value_stmt() {
  auto        c0  = lnast->get_child(cur);
  std::string lhs = c0.is_invalid() ? std::string{} : std::string(strip_prefix(lnast->get_name(c0)));
  print(decl_prefix(lhs));
  print(lhs);
  print(" = ");
  print(render_def_rhs(cur, /*operand_ctx=*/false));
}

void Lnast_prp_writer::write_range() {
  auto        c0  = lnast->get_child(cur);
  std::string lhs = c0.is_invalid() ? std::string{} : std::string(strip_prefix(lnast->get_name(c0)));
  print(decl_prefix(lhs));
  print(lhs);
  print(" = ");
  print(render_def_rhs(cur, /*operand_ctx=*/false));
}

// ── type_spec ───────────────────────────────────────────────────────────────
// `type_spec(ref(var), type)` is a bare type assertion the runner emits for an
// inlined-call temp.  scan_node pre-records the type in type_specs_, and the
// variable's first declaration (write_store) folds it in as `mut x:T = v`, so
// the standalone statement emits nothing here.  The annotation is inert (a
// type check) — when no declaring write consumes it, dropping it is sound: the
// recompile re-infers the same type.
void Lnast_prp_writer::write_type_spec() {}

// ── Pipeline stage annotations ──────────────────────────────────────────────

Lnast_nid Lnast_prp_writer::find_stages_child(Lnast_nid nid) const {
  for (auto c = lnast->get_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (Lnast_ntype::is_stages(lnast->get_type(c))) {
      return c;
    }
  }
  return {};
}

bool Lnast_prp_writer::emits_nothing_stmt(Lnast_nid nid) const {
  auto t = lnast->get_type(nid);
  if (t == Lnast_ntype::Lnast_ntype_type_spec) {
    return true;  // folded into a declaration
  }
  if (t == Lnast_ntype::Lnast_ntype_timecheck) {
    return true;  // inert; dropped (timing already carried by stage[N]/@[N])
  }
  if (t == Lnast_ntype::Lnast_ntype_declare && !find_stages_child(nid).is_invalid()) {
    return true;  // stage declare — re-attached to its store as `stage[N] x = v`
  }
  return false;
}

std::string Lnast_prp_writer::format_stages(Lnast_nid stages_nid) const {
  auto lo = lnast->get_child(stages_nid);
  if (lo.is_invalid()) {
    return {};
  }
  auto        hi  = lnast->get_sibling_next(lo);
  std::string los(lnast->get_name(lo));
  std::string his = hi.is_invalid() ? los : std::string(lnast->get_name(hi));
  // The slang reader stamps a `stages(nil,nil)` on every output port (no
  // explicit pipe depth); emit it as the opt-out `@[]`, not `@[nil]`.
  if (los.empty() || los == "nil") {
    return {};
  }
  if (los == his) {
    return los;  // pipe[N] / @[N]
  }
  if (his == "0") {
    return {};  // bare-pipe (min,0) sentinel: max unconstrained -> @[]
  }
  return los + "..=" + his;  // pipe[A..=B] / @[A..=B]
}
