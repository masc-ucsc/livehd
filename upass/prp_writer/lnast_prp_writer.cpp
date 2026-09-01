//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_prp_writer.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

// ── Constructor ───────────────────────────────────────────────────────────────

Lnast_prp_writer::Lnast_prp_writer(std::ostream& _os, std::shared_ptr<Lnast> _lnast) : os(_os), lnast(std::move(_lnast)) {}

// ── Public entry point ────────────────────────────────────────────────────────

void Lnast_prp_writer::prepare() {
  if (prepared_) {
    return;
  }
  prepared_ = true;
  depth     = 0;
  nid_stack = {};
  scan_nested_defs();       // feeds both statement drops below
  scan_dead_init_stores();  // BEFORE analyze_folding: a dropped dead store makes
                            // the surviving def single, hence inlinable
  analyze_folding();        // decide which single-use temps to inline
  index_store_timechecks();
  scan_file_imports();
}

// Dead initial stores. uPass hands the writer a body that seeds a name and then
// immediately overwrites it:
//
//   t194 = 0            <- dead: nothing reads t194 before the next def
//   t194 = tt194_97
//
// and, for a re-nested bundle, the same shape per field (`sig.cmd = 0` right
// after the tuple literal already set `mut cmd = 0`). Emitting the seed costs a
// dead line, re-initializes a field the tuple literal just initialized, and — the
// expensive part — makes the name look MULTI-DEF, which blocks analyze_folding
// from inlining an otherwise single-use temp. Drop a def when a LATER def of the
// same name follows with no read in between.
//
// Deliberately narrow: top-level statements only (the ordering below is a
// top-level index), and only a def whose value is a CONSTANT — a seed, never
// content whose evaluation could matter.
// Names stored inside a NESTED scope (an if arm, a loop body). For those, the
// top-level declaration/seed is NOT redundant, however dead its value looks —
// it is the unconditional binding the nested store writes to:
//
//   cin_o = 0ub????                      <- a seed, overwritten below, never read
//   cin_o = cin_o__w1
//   if ph__w1 != 0 { cin_o = t526 }
//
//   mut `ret_s36.ssip` = 0               <- a declare whose value the next def kills
//   `ret_s36.ssip` = (t13573 >> 1) & 1
//   if … { `ret_s36.ssip` = … }
//
// Drop the first line of either and the second becomes the binding def, making
// the third a rebind: `const `cin_o` rebind (assigned 2 times)`. Neither can be
// repaired downstream — an output port cannot be re-declared `mut` later — so
// both statement drops below leave these names alone.
void Lnast_prp_writer::scan_nested_defs() {
  auto stmts = body_stmts_nid();
  if (stmts.is_invalid()) {
    return;
  }
  std::vector<Lnast_nid> work;
  auto                   push_kids = [&](Lnast_nid n) {
    for (auto g = lnast->get_child(n); !g.is_invalid(); g = lnast->get_sibling_next(g)) {
      work.push_back(g);
    }
  };
  for (auto c = lnast->get_child(stmts); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    push_kids(c);  // start BELOW each top-level statement: only nested defs count
  }
  while (!work.empty()) {
    auto n = work.back();
    work.pop_back();
    const auto t = lnast->get_type(n);
    auto       v = lnast->get_child(n);
    if (!v.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(v)) && defines_child0(t) && !Lnast_ntype::is_declare(t)) {
      nested_def_names_.insert(std::string(strip_prefix(lnast->get_name(v))));
    }
    push_kids(n);
  }
}

void Lnast_prp_writer::scan_dead_init_stores() {
  auto stmts = body_stmts_nid();
  if (stmts.is_invalid()) {
    return;
  }
  absl::flat_hash_map<std::string, Lnast_nid> pending;  // name -> its last un-read const def
  size_t                                      idx = 0;
  for (auto c = lnast->get_child(stmts); !c.is_invalid(); c = lnast->get_sibling_next(c), ++idx) {
    const auto t = lnast->get_type(c);
    auto       v = lnast->get_child(c);

    // Any READ of a pending name keeps that def alive.
    absl::flat_hash_set<std::string> reads;
    if (!v.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(v)) && defines_child0(t) && !Lnast_ntype::is_declare(t)) {
      collect_driver_reads(c, reads);
    } else {
      collect_node_reads(c, reads);
    }
    for (const auto& r : reads) {
      pending.erase(r);
    }

    if (v.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(v)) || !defines_child0(t) || Lnast_ntype::is_declare(t)) {
      continue;
    }
    // attr_set defines metadata attached to child0, not the variable's value.
    // In particular, slang may place `attr_set rst_q clock_pin clk_i` after
    // the flop's constant din store. Treating that attribute as a later value
    // definition drops `rst_q = 1` and turns a reset synchronizer into a flop
    // that holds its reset value forever. Keep the pending value candidate;
    // a later real value definition can still supersede it.
    if (t == Lnast_ntype::Lnast_ntype_attr_set) {
      continue;
    }
    const std::string nm(strip_prefix(lnast->get_name(v)));
    // This def supersedes a pending one: that earlier store is dead.
    if (auto it = pending.find(nm); it != pending.end()) {
      if (nested_def_names_.count(nm) == 0) {
        dead_init_stmts_.insert(it->second.get_class_index().value);
      }
      pending.erase(it);
    }
    // A plain `X = <const>` scalar store becomes the next candidate.
    if (t != Lnast_ntype::Lnast_ntype_store) {
      continue;
    }
    auto val = lnast->get_sibling_next(v);
    if (val.is_invalid() || !lnast->is_last_child(val) || lnast->get_type(val) != Lnast_ntype::Lnast_ntype_const) {
      continue;
    }
    pending.emplace(nm, c);
  }
}

// The body `stmts` of this unit: the sibling after `io` for an extracted lambda,
// the file scope's own `stmts` otherwise.
Lnast_nid Lnast_prp_writer::body_stmts_nid() const {
  auto root = lnast->get_root();
  for (auto c = lnast->get_child(root); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (lnast->get_type(c) == Lnast_ntype::Lnast_ntype_io) {
      auto b = lnast->get_sibling_next(c);
      return (!b.is_invalid() && lnast->get_type(b) == Lnast_ntype::Lnast_ntype_stmts) ? b : Lnast_nid{};
    }
    if (lnast->get_type(c) == Lnast_ntype::Lnast_ntype_stmts) {
      return c;
    }
  }
  return {};
}

void Lnast_prp_writer::collect_header() {
  prepare();
  write_module_imports();  // routed to header_sink_ (set by the group emit)
}

void Lnast_prp_writer::write_all() {
  prepare();
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

// The `t<N>` / `tt<N>_<M>` names emit_name_for mints for `%`-prefixed compiler
// temps. Re-reading a generated file turns them back into ordinary source names,
// so the fold policy needs to recognise the spelling to keep treating them as
// the temps they are.
bool Lnast_prp_writer::is_writer_temp_name(std::string_view name) {
  auto digits = [](std::string_view s) { return !s.empty() && s.find_first_not_of("0123456789") == std::string_view::npos; };
  if (name.size() > 1 && name[0] == 't' && digits(name.substr(1))) {
    return true;  // t157
  }
  if (name.size() > 3 && name.substr(0, 2) == "tt") {
    const auto us = name.find('_', 2);
    return us != std::string_view::npos && digits(name.substr(2, us - 2)) && digits(name.substr(us + 1));  // tt343_0
  }
  return false;
}

bool Lnast_prp_writer::is_tmp(std::string_view name) const {
  // A raw compiler temp (`%`-prefix), OR a name strip_prefix already mapped to
  // its emittable `t<id>` form.
  if (!name.empty() && name[0] == '%') {
    return true;
  }
  return emitted_tmp_names_.contains(std::string(name));
}

void Lnast_prp_writer::seed_emit_names() const {
  // One-time seed: every NON-temp ref name in the tree is reserved, so a
  // synthesised name can never collide with a user identifier or port.
  if (emit_names_seeded_) {
    return;
  }
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

std::string Lnast_prp_writer::ssa_emit_name_for(std::string_view name, size_t pos) const {
  // `<base>___ssa_<N>` -> `<base>__w<N>`, but `__wN` is NOT a free namespace:
  // THIS writer mints it, so any Pyrope it generated is full of source-level
  // `__wN` names, and a second trip through the writer can demote onto one.
  // That would put two distinct variables under one emitted identifier — the
  // emitted source then silently means something else (the `q` of
  // pass/prp_writer/tests/ssa_demote_collision.prp stops reading its input).
  // So reserve against every name already in the tree and bump the VERSION
  // until it is free, memoizing so the def and every use agree.
  seed_emit_names();
  auto cached = ssa_emit_names_.find(std::string(name));
  if (cached != ssa_emit_names_.end()) {
    return cached->second;
  }
  const std::string base(name.substr(0, pos));
  uint64_t          version = 0;
  for (size_t i = pos + 7; i < name.size() && version < (1ULL << 40); ++i) {
    version = version * 10 + static_cast<uint64_t>(name[i] - '0');
  }
  std::string cand;
  do {
    cand = base + "__w" + std::to_string(version);
    ++version;
  } while (used_emit_names_.contains(cand));
  used_emit_names_.insert(cand);
  ssa_emit_names_.emplace(std::string(name), cand);
  return cand;
}

std::string Lnast_prp_writer::emit_name_for(std::string_view tmp) const {
  seed_emit_names();
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

// True for infix operators that are fully associative, so a same-operator
// chain `(((a op b) op c) op d)` can drop its redundant parens and print flat
// as `a op b op c op d`.  This keeps a wide N-operand reduction (e.g. a 4096-bit
// next-state bit-assembly) from nesting parens N levels deep and tripping the
// prp parser's recursion guard (inou/prp/prp2lnast.cpp kMaxParseNesting).  Only
// operators where re-association is value-preserving qualify; `-`, `/`, `%`,
// shifts and comparisons are intentionally excluded.
static bool is_associative_optype(Lnast_ntype::Lnast_ntype_int t) {
  using N = Lnast_ntype;
  switch (t) {
    case N::Lnast_ntype_plus:
    case N::Lnast_ntype_mult:
    case N::Lnast_ntype_bit_and:
    case N::Lnast_ntype_bit_or:
    case N::Lnast_ntype_bit_xor:
    case N::Lnast_ntype_log_and:
    case N::Lnast_ntype_log_or : return true;
    default                    : return false;
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
    case N::Lnast_ntype_concat:
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
  // A bundle-field leaf (`io.result`) is already declared via its base bundle
  // (`wire io:(...)`); a write to it is a plain field assignment, never a new
  // `mut io.result` declaration (which is an illegal tuple-path lvalue decl).
  if (is_bundle_field(lhs)) {
    return {};
  }
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
    // …unless it is written more than once after all (a lane write
    // `t#[hi..=lo] = v` is a second def), in which case `const` is a rebind.
    return multi_def_tmp_.count(std::string(lhs)) ? "mut " : "const ";
  }
  if (declared_.count(std::string(lhs))) {
    return {};
  }
  declared_.insert(std::string(lhs));
  // A net defined exactly once, by this very (top-level) store, with no earlier
  // read: it was NOT hoisted to a `mut X = 0` prologue line, so this store is its
  // declaration — and a single-assignment net is a `const`, not a `mut`.  Besides
  // dropping the prologue line and the dead `= 0` store, `const` keeps the
  // recompile from range-unioning the seed `0` into the net's inferred range.
  if (pkg_valued_store_.count(std::string(lhs))) {
    return "mut ";  // a comptime-const-valued single store must stay runtime (see the set's doc)
  }
  if (single_store_.count(std::string(lhs))) {
    return "const ";
  }
  return "mut ";
}

// A bare Pyrope reserved word used as a variable identifier (e.g. a Verilog
// signal named `wrap`/`sat`/`reg`/`stage`) re-parses as a keyword and breaks the
// recompile leg (`wrap = x` parses as the overflow modifier + a broken
// assignment -> "expected an expression").  Such a name must be backtick-escaped
// on emit; the lexer strips the backticks back to the identical name, so the lg
// name (and LEC matching) is unaffected.
//
// The set is prpparse's OWN keyword table (the same X-macro the lexer reads),
// not a hand-kept copy.  The copy this replaces had drifted 13 words behind the
// parser — `case`, `does`, `equals`, `formal`, `has`, `implies`, `integer`,
// `sext`, `signed`, `stage`, `tick`, `unsigned`, `zext` — and each one emitted a
// file lhd could not re-parse.  `stage` was the expensive one: a Verilog shift
// register named `stage` (bedrock's br_delay_valid) emits `stage[0] = a`, which
// re-parses as a `stage[N]` pipelining declaration ("expected an expression").
static bool is_pyrope_reserved_ident(std::string_view s) {
  // clang-format off: the `#include` inside the braced list makes clang-format
  // break the hand-added words one per line, which buries them.
  static const absl::flat_hash_set<std::string_view> kw = {
      // Reserved by the docs or held for future syntax, so absent from the
      // parser's table; over-quoting a non-keyword is harmless.
      "nil", "where", "priority", "defer", "async", "await", "cpp",
#define PRP_KEYWORD(name) #name,
#include "prpparse/prp_keywords.def"
  };
  // clang-format on
  return kw.contains(s);
}

// A reconstructed bundle path (`in.bits`) is emitted as the BARE dotted path so
// it re-parses as tuple-field access (not a quoted opaque leaf).  But a base or
// field component that collides with a Pyrope keyword (`in.bits`, `x.reg`) would
// then re-lex AS the keyword and break the path ("expected an expression" at the
// `.`).  Backtick-escape each keyword component INDIVIDUALLY — `` `in`.bits `` —
// so the dots stay field separators; the lexer strips the backticks, so the lg
// name (and LEC matching) is unchanged.  Non-keyword components stay bare, so a
// normal path (`io.operation`) is emitted byte-identical to before.
// A mod/pipe callee bound via a DOTTED import folds (constprop) to a string Dlop, so
// the callee const arrives QUOTED (`'Unit.Entity'`) where a same-file ref callee stays
// bare (`Unit.Entity`) — see upass/tolg's lower_func_call, which strips the same quotes
// for the same reason. Unstripped, the quotes (a) make the known_modules_ lookup that
// decides whether to emit `const X = import(..)` MISS, so no import is emitted at all,
// and (b) print verbatim as `'Unit.Entity'(args)`, which is not a call — the emitted
// file does not parse.
static std::string_view unquote_callee(std::string_view s) {
  if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

// A Verilog ESCAPED identifier (`\s\m`, `\a-b`, `\1foo`) is a perfectly legal
// module name, and it arrives at the writer verbatim.  Signal names go through
// strip_prefix's `quote` lambda; MODULE names did not, so `module \s\m` emitted
// `pub comb s\m::[…]` — and `\` is not a legal Pyrope token, so the generated
// file could not even be TOKENIZED ("unexpected character in input").  Same
// leak at the two other places a module name reaches the text: the file-scope
// `const <name> = import(…)` binding and the `<name>::[name=u1](…)` call site.
// A backtick-quoted name lexes back to the identical identifier (verified for
// `.`/`-`/`\`/space/`$`/keyword spellings), so quoting is always safe; the
// predicate below keeps a normal name BYTE-IDENTICAL to before.
static std::string escape_string(std::string_view s);  // defined with the string-literal writers below

static bool is_pyrope_ident_spelling(std::string_view s) {
  if (s.empty() || (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_')) {
    return false;  // empty, or starts with a digit / punctuation
  }
  for (char c : s) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '$') {
      return false;
    }
  }
  return true;
}

static bool is_plain_pyrope_ident(std::string_view s) { return is_pyrope_ident_spelling(s) && !is_pyrope_reserved_ident(s); }

// Index of the next `.` at or after `from` that is NOT inside a backtick-quoted
// span, or npos.  A Pyrope-origin escaped identifier arrives ALREADY wrapped
// (prp2lnast's canonical_escaped_ident keeps the quotes), and a `\a.b` Verilog
// escaped id then carries a dot that belongs to the NAME, not to the
// file/entity separator — splitting on it would cut the quoted span in half.
static size_t next_unquoted_dot(std::string_view path, size_t from) {
  bool in_tick = false;
  for (size_t i = from; i < path.size(); ++i) {
    if (path[i] == '`') {
      in_tick = !in_tick;
    } else if (path[i] == '.' && !in_tick) {
      return i;
    }
  }
  return std::string_view::npos;
}

// Strip one surrounding pair of escape backticks, if present.  Quoting has to be
// IDEMPOTENT: the same name reaches this decorator both bare (slang provenance:
// `d\e`) and already-escaped (Pyrope provenance: `` `s\m` ``), and re-wrapping
// the latter emitted ``s\m`` — two adjacent quoted spans the lexer cannot read
// at all ("unexpected character in input").
static std::string_view peel_escape_ticks(std::string_view s) {
  if (s.size() >= 2 && s.front() == '`' && s.back() == '`') {
    s.remove_prefix(1);
    s.remove_suffix(1);
  }
  return s;
}

// Backtick-escape each `.`-component of a module path that is not a plain Pyrope
// identifier.  Per-component (not whole-name) so a Pyrope-origin `file.entity`
// callee (`ALU.ALU`) keeps its dot as the file/entity separator instead of
// collapsing into one opaque quoted leaf.
static std::string quote_module_path(std::string_view path) {
  std::string out;
  size_t      start = 0;
  for (;;) {
    auto             dot  = next_unquoted_dot(path, start);
    std::string_view comp = path.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
    comp                  = peel_escape_ticks(comp);
    if (is_plain_pyrope_ident(comp)) {
      out.append(comp);
    } else {
      out.push_back('`');
      out.append(comp);
      out.push_back('`');
    }
    if (dot == std::string_view::npos) {
      break;
    }
    out.push_back('.');
    start = dot + 1;
  }
  return out;
}

static std::string quote_kw_path(std::string_view path) {
  std::string out;
  size_t      start = 0;
  for (;;) {
    auto             dot  = path.find('.', start);
    std::string_view comp = path.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
    if (is_pyrope_reserved_ident(comp)) {
      out.push_back('`');
      out.append(comp);
      out.push_back('`');
    } else {
      out.append(comp);
    }
    if (dot == std::string_view::npos) {
      break;
    }
    out.push_back('.');
    start = dot + 1;
  }
  return out;
}

// A flattened nested output key (`rsp.header.id`) can be rendered as the
// equivalent Pyrope field path instead of a string-key lookup.  Require every
// component to be an identifier spelling: an escaped Verilog output name may
// contain a literal dot, and preserving its bracket-string lookup is safer than
// accidentally turning that dot into bundle traversal.  Keyword components are
// individually escaped by quote_kw_path (a field named `reg` is backticked).
static std::optional<std::string> quote_field_path(std::string_view path) {
  size_t start = 0;
  for (;;) {
    const auto dot  = path.find('.', start);
    const auto comp = path.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
    if (!is_pyrope_ident_spelling(comp)) {
      return std::nullopt;
    }
    if (dot == std::string_view::npos) {
      return quote_kw_path(path);
    }
    start = dot + 1;
  }
}

// `pkg.PARAM` where pkg is an imported package (provenance flow): a real
// bundle-field access on the import namespace, emitted as a bare dotted path so
// it re-parses (NOT a backtick-escaped opaque leaf, NOT a `pkg["PARAM"]` index).
bool Lnast_prp_writer::is_imported_package_name(std::string_view name) const {
  for (const auto& pkg : lnast->get_imported_packages()) {
    if (pkg == name) {
      return true;
    }
  }
  return false;
}

bool Lnast_prp_writer::is_imported_pkg_path(std::string_view name) const {
  const auto dot = name.find('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  return is_imported_package_name(name.substr(0, dot));
}

bool Lnast_prp_writer::is_bundle_field(std::string_view name) const {
  const auto dot = name.find('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  // The caller may hand us either the RAW path (`in.bits`, from the quote lambda)
  // or the already-escaped path (`` `in`.bits ``, from decl_prefix on a
  // strip_prefix'd lhs).  bundle_fields_ is keyed by the BARE base/field, so peel
  // a component's surrounding backticks before the lookup.
  auto unquote = [](std::string_view s) -> std::string_view {
    if (s.size() >= 2 && s.front() == '`' && s.back() == '`') {
      return s.substr(1, s.size() - 2);
    }
    return s;
  };
  auto it = bundle_fields_.find(std::string(unquote(name.substr(0, dot))));
  if (it == bundle_fields_.end()) {
    return false;
  }
  return it->second.count(std::string(unquote(name.substr(dot + 1)))) != 0;
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
  auto quote = [this](std::string s) -> std::string {
    // A name read from an escaped Verilog id arrives ALREADY backtick-quoted
    // (`` `ar.x` `` — the dlop quoted-identifier form). Emit it verbatim; wrapping
    // it again yields `` ``ar.x`` `` which the Pyrope lexer rejects (the v2prp
    // round-trip then fails to re-parse). Only a bare `.`-name needs quoting.
    if (s.size() >= 2 && s.front() == '`' && s.back() == '`') {
      // …unless collect_body_bundles re-nested this leaf: the escaped
      // `` `sig.cmd` `` IS a field of the rebuilt bundle now, so emit the bare
      // dotted path (`sig.cmd`) that reads it back out of the tuple.
      if (const auto inner = s.substr(1, s.size() - 2); is_bundle_field(inner)) {
        return quote_kw_path(inner);
      }
      return s;
    }
    // Same quoted name, but with a VERSION SUFFIX pasted after its closing backtick
    // (`` `ar.x`__w1 ``). Both strip_prefix (below) and upass.ssa's twin demotion
    // rename `<base>___ssa_<N>` -> `<base>__w<N>` by appending to a base that may
    // ALREADY be quoted, so the front-AND-back test above misses it and the
    // fall-through re-wraps it into `` ``ar.x`__w1` `` -- which the lexer rejects, so
    // the emitted file does not re-parse AT ALL. (That silently broke `lhd lec`'s
    // simfail witness testbench -- 217 such names in one re-emitted module -- and
    // every other prp->prp round-trip carrying an escaped-id SSA version.)
    // Fold the quotes OUTSIDE the whole name: `` `ar.x__w1` `` lexes to the single
    // identifier `ar.x__w1` -- still one name, still distinct per version, and both
    // the def and every use come through here, so they agree.
    // Gated on the exact `__w<digits>` version form so a per-component escaped PATH
    // (`` `in`.bits ``, from quote_kw_path / decl_prefix) is left alone.
    if (s.size() >= 2 && s.front() == '`') {
      if (const auto close = s.find('`', 1); close != std::string::npos && s.find('`', close + 1) == std::string::npos) {
        const std::string_view suf = std::string_view{s}.substr(close + 1);
        if (suf.size() > 3 && suf.substr(0, 3) == "__w" && suf.find_first_not_of("0123456789", 3) == std::string_view::npos) {
          return "`" + s.substr(1, close - 1) + std::string(suf) + "`";
        }
      }
    }
    // A reconstructed bundle field (`io.operation`) is a REAL tuple-field access,
    // not an opaque dotted leaf name — emit the bare dotted path so it re-parses
    // as `io.operation` (detuple re-splits it), instead of a quoted leaf.  A
    // keyword base/field (`in.bits`) still needs its colliding component escaped
    // (`` `in`.bits ``) so the path re-parses, hence quote_kw_path not bare `s`.
    if (is_bundle_field(s) || is_imported_pkg_path(s)) {
      return quote_kw_path(s);
    }
    return (s.find('.') == std::string::npos && !is_pyrope_reserved_ident(s)) ? s : "`" + s + "`";
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
  return quote(ssa_emit_name_for(name, pos));
}

// ── Main dispatch ─────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_node() {
  using N = Lnast_ntype;
  switch (current_ntype()) {
    case N::Lnast_ntype_top          : write_top(); break;
    case N::Lnast_ntype_stmts        : write_stmts(); break;
    case N::Lnast_ntype_if           : write_if(); break;
    case N::Lnast_ntype_unique_if    : write_if(); break;  // prints `unique if`
    case N::Lnast_ntype_declare      : write_declare(); break;
    case N::Lnast_ntype_store        : write_store(); break;
    case N::Lnast_ntype_ref          : write_ref(); break;
    case N::Lnast_ntype_const        : write_const(); break;
    case N::Lnast_ntype_cassert      : write_cassert(); break;
    case N::Lnast_ntype_func_call    : write_func_call(); break;
    case N::Lnast_ntype_func_def     : write_func_def(); break;
    case N::Lnast_ntype_for          : write_for(); break;
    case N::Lnast_ntype_rolled_for   : write_rolled_for(); break;
    case N::Lnast_ntype_func_break   : print("break"); break;
    case N::Lnast_ntype_func_continue: print("continue"); break;
    case N::Lnast_ntype_func_return  : print("return"); break;
    // timecheck (`x@[N]`) is an inert landing-cycle assertion. The timing it
    // carries is ALREADY re-emitted by the writer as the `stage[N]` declaration
    // and the `out:T@[N]` interface annotation, so the standalone statement is
    // redundant; and its SSA-renamed ref often names a stage var not yet
    // assigned at this point (`tmp@[3]` precedes `stage[3] tmp = …`), which
    // would forward-reference an undeclared name on re-parse. Drop it (the
    // `@[]` opt-out) — sound (inert) and loses no timing. See emits_nothing_stmt
    // so the statement loop leaves no blank line.
    case N::Lnast_ntype_timecheck    : break;
    case N::Lnast_ntype_tuple_add    : write_tuple_add(); break;
    case N::Lnast_ntype_tuple_concat : write_tuple_concat(); break;
    case N::Lnast_ntype_attr_set     : write_attr_set(); break;
    case N::Lnast_ntype_delay_assign : write_delay_assign(); break;
    case N::Lnast_ntype_set_mask     : write_set_mask(); break;
    case N::Lnast_ntype_range        : write_range(); break;
    case N::Lnast_ntype_type_spec    : write_type_spec(); break;
    // All value-producing ops share one statement wrapper; render_def_rhs()
    // spells the per-op RHS and inlines any single-use temp operands.
    case N::Lnast_ntype_plus         :
    case N::Lnast_ntype_minus        :
    case N::Lnast_ntype_mult         :
    case N::Lnast_ntype_div          :
    case N::Lnast_ntype_mod          :
    case N::Lnast_ntype_shl          :
    case N::Lnast_ntype_sra          :
    case N::Lnast_ntype_sext         :
    case N::Lnast_ntype_get_mask     :
    case N::Lnast_ntype_eq           :
    case N::Lnast_ntype_ne           :
    case N::Lnast_ntype_lt           :
    case N::Lnast_ntype_le           :
    case N::Lnast_ntype_gt           :
    case N::Lnast_ntype_ge           :
    case N::Lnast_ntype_log_and      :
    case N::Lnast_ntype_log_or       :
    case N::Lnast_ntype_log_not      :
    case N::Lnast_ntype_bit_and      :
    case N::Lnast_ntype_bit_or       :
    case N::Lnast_ntype_bit_xor      :
    case N::Lnast_ntype_bit_not      :
    case N::Lnast_ntype_tuple_get    :
    // concat has no infix/postfix spelling, but it is still a plain value def
    // (`dst = concat(a, b, c)`), so it rides the same wrapper — only its RHS
    // rendering (a call shape) differs. Keeping it here, rather than in a
    // private write_concat(), is what makes it work in the OTHER render_def_rhs
    // consumers too: a mux arm (arm_value_def) accepts any defines_child0 type,
    // and would render a concat arm as bare child0 — a silent lane drop — if
    // render_def_rhs did not spell it.
    case N::Lnast_ntype_concat       :
    case N::Lnast_ntype_attr_get     : write_value_stmt(); break;
    default                          : {
      // Unknown node — record it (the pass fails the compile unless debug) and
      // emit a comment so the output stays parseable.
      emit_unimplemented(
          std::format("unhandled node type {} ({})", static_cast<int>(current_ntype()), Lnast_ntype::to_sv(current_ntype())));
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
  write_module_imports();
  // A package namespace unit (slang provenance flow): emit the exports straight
  // from the pub list — `pub comptime const NAME[:type] = <defining expr |
  // folded value>`. The general const-declare path drops a comptime const's
  // folded value to `= 0`; the defining-expression/type riders come from the
  // reader (get_package_const_exprs/types), pub order IS source order.
  if (lnast->is_package_unit()) {
    absl::flat_hash_map<std::string, std::string> vals;
    for (const auto& [path, text] : lnast->get_pub_values()) {
      vals.emplace(path, text);
    }
    const auto& exprs = lnast->get_package_const_exprs();
    const auto& types = lnast->get_package_const_types();
    for (const auto& p : lnast->get_pub_list()) {
      if (p.kind == "type") {
        // scalar alias export: `pub type VPU_FCMD_SZ_T = u7`
        if (auto tit = types.find(p.name); tit != types.end()) {
          print("pub type ");
          print(p.name);
          print(" = ");
          print(tit->second);
          print("\n");
        }
        continue;
      }
      print("pub comptime const ");
      print(p.name);
      if (auto tit = types.find(p.name); tit != types.end()) {
        print(":");
        print(tit->second);
      }
      print(" = ");
      if (auto eit = exprs.find(p.name); eit != exprs.end()) {
        print(eit->second);
      } else {
        auto it = vals.find(p.name);
        print(it != vals.end() ? it->second : "0");
      }
      print("\n");
    }
    return;
  }
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
void Lnast_prp_writer::write_module_imports() {
  if (header_done_) {
    return;  // already produced (a group emit collected it before the bodies)
  }
  header_done_ = true;

  std::vector<std::string> lines;

  struct Import {
    std::string call_name;
    std::string module_name;
    bool        emitted_sibling{false};
  };
  const std::string   self(lnast->get_top_module_name());
  std::vector<Import> imports;
  // No emitted siblings and no blackbox list ⇒ nothing a callee could resolve to.
  const bool          resolvable = known_modules_ != nullptr || !lnast->get_external_modules().empty();
  for (const auto& call : func_call_callees_) {
    if (!resolvable) {
      break;
    }
    std::string resolved;
    bool        emitted_sibling = false;
    if (known_modules_ != nullptr) {
      if (known_modules_->contains(call)) {
        resolved        = call;
        emitted_sibling = true;
      } else {
        const std::string suffix = "." + call;
        for (const auto& candidate : *known_modules_) {
          if (candidate.size() > suffix.size() && candidate.ends_with(suffix)) {
            if (!resolved.empty()) {
              resolved.clear();  // ambiguous tail: a bare call cannot choose
              break;
            }
            resolved        = candidate;
            emitted_sibling = true;
          }
        }
      }
    }
    if (resolved.empty() && lnast->has_external_module(call)) {
      resolved = call;
    }
    if (resolved.empty() || resolved == self) {
      continue;
    }
    // A callee emitted into the SAME .prp (both units came from one source
    // file) needs no import — it is a sibling lambda in this file's scope, and
    // `const helper = import("f.helper")` next to `pub comb helper` in the same
    // file is a redeclaration.
    if (emitted_sibling && resolved.substr(0, resolved.find('.')) == self.substr(0, self.find('.'))) {
      same_file_callee_.emplace(call, resolved);
      continue;
    }
    imports.push_back({call, resolved, emitted_sibling});
  }
  std::sort(imports.begin(), imports.end(), [](const Import& lhs, const Import& rhs) {
    return std::tie(lhs.module_name, lhs.call_name) < std::tie(rhs.module_name, rhs.call_name);
  });

  // Every name the body DECLARES or WRITES, plus the ports, shares the namespace
  // of the file-scope `const <alias> = import(...)` binding: Verilog keeps nets
  // and modules apart (`logic popcount; popcount #(..) i_popcount (..)`, CVA6's
  // instr_queue), Pyrope does not, and a local of the callee's name shadows the
  // import so the instantiation re-reads as a call to the local's VALUE
  // (`call to undefined function '0'`). `def_count > 0` is the filter: a
  // callee spelled as a ref is only ever READ, so it never forces its own alias
  // aside, while an instance result (`mut x = X(..)`), a local, a temp or a port
  // does. inst_names is kept separately only because the loop below also has
  // to dodge a later instance that takes the alias.
  absl::flat_hash_set<std::string> inst_names;
  absl::flat_hash_set<std::string> body_names;
  for (const auto& [name, fi] : fold_info_) {
    if (fi.def_count > 0) {
      body_names.insert(std::string(strip_prefix(name)));
    }
    if (fi.def_count == 1 && fi.def_type == Lnast_ntype::Lnast_ntype_func_call) {
      inst_names.insert(std::string(strip_prefix(name)));
    }
  }
  for (const auto& e : lnast->io_meta().inputs) {
    body_names.insert(std::string(strip_prefix(e.name)));
  }
  for (const auto& e : lnast->io_meta().outputs) {
    body_names.insert(std::string(strip_prefix(e.name)));
  }
  import_alias_.clear();
  // A same-file sibling is referred to by its BARE lambda name — the qualified
  // `file.entity` spelling only resolves through an import const, and there is
  // none (nor may there be: it would redeclare the lambda).
  for (const auto& [call, resolved] : same_file_callee_) {
    const auto  dot         = resolved.rfind('.');
    std::string alias       = dot == std::string::npos ? resolved : resolved.substr(dot + 1);
    import_alias_[call]     = alias;
    import_alias_[resolved] = alias;
  }
  for (const auto& imp : imports) {
    const auto  dot   = imp.module_name.rfind('.');
    std::string alias = dot == std::string::npos ? imp.module_name : imp.module_name.substr(dot + 1);
    if (inst_names.contains(alias) || body_names.contains(alias)) {
      do {
        alias += "_t";
      } while (inst_names.contains(alias) || body_names.contains(alias)
               || (known_modules_ != nullptr && known_modules_->contains(alias)));
    }

    // `import("<file>.<entity>")` — the file the sibling lands in, then the pub
    // lambda inside it. pass.prp_writer groups every unit of one source file
    // into `<file>.prp`, so a Pyrope-origin unit is ALREADY spelled
    // `file.entity` and needs no suffix; a slang-origin unit is the bare module
    // name and its file holds a lambda of the same name (`m` -> `m.m`). Getting
    // this wrong grows the path one level per round trip.
    const std::string path         = dot == std::string::npos ? imp.module_name + "." + imp.module_name : imp.module_name;
    import_alias_[imp.call_name]   = alias;
    import_alias_[imp.module_name] = alias;
    lines.emplace_back("const " + quote_module_path(alias) + " = import(\"" + escape_string(path) + "\")\n");
  }

  // File-scope package imports (provenance flow): one `const pkg = import("pkg")`
  // per referenced package, so the `pkg.PARAM` refs resolve on recompile. A
  // lambda-body import does not lower, so these MUST sit at file scope. An SV
  // package can be named for a Pyrope keyword (`match`, `mut`, `step`, …) or be
  // a Verilog escaped id, neither of which lexes bare — hence quote_module_path
  // on the binding and escape_string on the (string-literal) path.
  for (const auto& pkg : lnast->get_imported_packages()) {
    lines.emplace_back("const " + quote_module_path(pkg) + " = import(\"" + escape_string(pkg) + "\")\n");
  }

  // A Pyrope FILE-level unit carries the source's own `const X = import("…")`
  // bindings as elaboration residue (scan_file_imports). Re-emit them from the
  // recovered (alias, path) so the file keeps its imports; the body drops the
  // residue statements.
  for (const auto& [alias, path] : file_imports_) {
    lines.emplace_back("const " + quote_module_path(alias) + " = import(\"" + escape_string(path) + "\")\n");
  }

  if (header_sink_ != nullptr) {
    // Group emit: the pass hoists one deduped header above every unit of the
    // file (two lambdas in one file share their submodule imports). An IDENTICAL
    // line is the same binding and is dropped. A line that reuses a bound name
    // for a DIFFERENT path is not — alias selection runs per unit and never sees
    // the shared sink, so one unit importing `a.mul` and a sibling importing
    // `b.mul` both pick `mul`; keeping only the first would silently instantiate
    // the wrong submodule. Give the loser a fresh, sink-unique alias and rewrite
    // this unit's call sites through import_alias_.
    for (auto& l : lines) {
      if (std::find(header_sink_->begin(), header_sink_->end(), l) != header_sink_->end()) {
        continue;  // byte-identical binding already present
      }
      const auto  eq      = l.find(" = ");
      std::string bound   = eq == std::string::npos ? l : l.substr(0, eq);  // "const <alias>"
      auto        clashes = [&](const std::string& b) {
        return std::any_of(header_sink_->begin(), header_sink_->end(), [&](const std::string& have) {
          return have.compare(0, b.size(), b) == 0 && have.size() > b.size() && have[b.size()] == ' ';
        });
      };
      if (eq != std::string::npos && clashes(bound)) {
        const std::string alias = bound.substr(std::string_view("const ").size());
        std::string       fresh = alias;
        std::string       fresh_bound;
        do {
          fresh       += "_i";
          fresh_bound  = "const " + fresh;
        } while (clashes(fresh_bound));
        for (auto& [call, a] : import_alias_) {
          if (a == alias) {
            a = fresh;
          }
        }
        l = fresh_bound + l.substr(eq);
      }
      header_sink_->emplace_back(std::move(l));
    }
    return;
  }
  for (const auto& l : lines) {
    os << l;
  }
  if (!lines.empty()) {
    os << "\n";
  }
}

void Lnast_prp_writer::write_module() {
  Lnast_nid io_nid = cur;

  const bool verilog_origin = lnast->is_verilog_origin();
  const bool is_pipe        = !verilog_origin && lnast->get_lambda_kind() == "pipe";
  const bool is_mod
      = is_pipe || (!verilog_origin && lnast->get_lambda_kind() == "mod") || body_has_state(lnast->get_sibling_next(io_nid));
  // Re-nest flattened tuple-port leaves BEFORE the header (and before any body
  // pass caches a stripped name): fills port_group_text_/port_group_skip_ for
  // emit_port_group, plus bundle_fields_/declared_ so body accesses print the
  // bare dotted path and never re-declare a leaf.
  collect_port_groups(io_nid, is_mod && !is_pipe);
  collect_body_bundles(lnast->get_sibling_next(io_nid));
  // Name decoration depends on the bundle indexes populated above. Discard
  // any entries memoized by prepare-time analyses before those indexes existed.
  stripped_name_cache_.clear();
  node_read_ids_cache_.clear();
  print("pub ");
  if (is_pipe) {
    print("pipe");
    auto in_tup  = lnast->get_child(io_nid);
    auto out_tup = in_tup.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(in_tup);
    auto output  = out_tup.is_invalid() ? Lnast_nid{} : lnast->get_child(out_tup);
    auto stages  = output.is_invalid() ? Lnast_nid{} : find_stages_child(output);
    if (!stages.is_invalid()) {
      auto lo = lnast->get_child(stages);
      auto hi = lo.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(lo);
      if (!lo.is_invalid() && (hi.is_invalid() || lnast->get_name(hi) != "0")) {
        print("[");
        print(format_stages(stages));
        print("]");
      }
    }
    print(" ");
  } else {
    print(is_mod ? "mod " : "comb ");
  }
  print(lambda_name());
  // `timecheck=false` opts the re-compile out of the Pyrope timing / comb-cycle
  // checks (plain regs = always_ff cycle-0 state, undriven wire = X, same-cycle
  // wire ring not flagged as a comb loop) — the semantics a Verilog-imported unit
  // needs. The former `lg="<name>"` module-name pin is GONE: the internal graph
  // name is always the unique `file.entity` (Verilog flattens to the entity at
  // emission), so re-emitting an inert `lg=` would only mislead. A re-compiled
  // `fun3` is named `<file>.fun3` exactly as the original was.
  if (!is_pipe && lnast->is_verilog_origin()) {
    print("::[timecheck=false]");
  }
  emit_module_header(io_nid, is_mod && !is_pipe);
  print(" {\n");
  ++depth;

  auto stmts_nid = lnast->get_sibling_next(io_nid);
  collect_folded_attrs(stmts_nid);  // gather reg/mem attrs to fold into declares
  if (!stmts_nid.is_invalid()) {
    // A `clock_pin=ref X` / `reset_pin=ref X` on a reg/latch declare READS X
    // at the declare's emission point, and the declare pass emits every
    // declare at the top of the function — usually before X's driver.  The
    // writer used to fix this by RELOCATING X's whole dependency cone above
    // the declares, with its own dependency graph, topological sort and a
    // cycle-repair fallback — the machinery behind the fixme 1f/1f-bis silent
    // miscompiles.  It now makes X POSITION-INDEPENDENT instead and leaves
    // every statement in body order: see the pin_wire_hoist / pin_alias
    // decision below.
    // Reads of an `if`/`unique_if`: condition operands plus every arm
    // statement's operands (written lhs excluded), fold-following throughout.
    auto collect_if_reads = [&](auto&& self, Lnast_nid n, absl::flat_hash_set<std::string>& out) -> void {
      for (auto b = lnast->get_child(n); !b.is_invalid(); b = lnast->get_sibling_next(b)) {
        if (!Lnast_ntype::is_stmts(lnast->get_type(b))) {
          collect_node_reads(b, out);  // condition
          continue;
        }
        for (auto s = lnast->get_child(b); !s.is_invalid(); s = lnast->get_sibling_next(s)) {
          const auto st = lnast->get_type(s);
          auto       s0 = lnast->get_child(s);
          if (st == Lnast_ntype::Lnast_ntype_if || st == Lnast_ntype::Lnast_ntype_unique_if) {
            self(self, s, out);
          } else if (!s0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(s0)) && defines_child0(st)) {
            collect_driver_reads(s, out);
          } else {
            collect_node_reads(s, out);
          }
        }
      }
    };
    // A `wire` net is POSITION-INDEPENDENT: it is hoisted to the function top as
    // a bare `wire X:T` DECLARATION and its store stays its sole driver, so a
    // `clock_pin=ref X` binds correctly wherever the store lands. Any emitted
    // ICG module shows the pattern: `wire clkgt:u1` at the top, the regs
    // declared with `clock_pin=ref clkgt`, and `clkgt = <gate>` assigned after
    // the body.
    // A `reg`/`latch` net is position-independent too: its declare is emitted
    // by the declare pass and a read of its Q is order-free flop state, so a
    // `clock_pin=ref <reg>` (a divided clock: `always_ff @(posedge div_q)`)
    // binds the Q pin wherever the flop's next-state store lands.
    absl::flat_hash_set<std::string> wire_decl;
    absl::flat_hash_set<std::string> state_decl_pre;
    {
      auto scan_wires = [&](auto&& self, Lnast_nid n) -> void {
        for (auto s = lnast->get_child(n); !s.is_invalid(); s = lnast->get_sibling_next(s)) {
          const auto st = lnast->get_type(s);
          if (Lnast_ntype::is_stmts(st) || st == Lnast_ntype::Lnast_ntype_if || st == Lnast_ntype::Lnast_ntype_unique_if) {
            self(self, s);
            continue;
          }
          if (st != Lnast_ntype::Lnast_ntype_declare) {
            continue;
          }
          auto d0 = lnast->get_child(s);
          if (d0.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(d0))) {
            continue;
          }
          auto d1 = lnast->get_sibling_next(d0);
          auto d2 = d1.is_invalid() ? d1 : lnast->get_sibling_next(d1);
          if (!d2.is_invalid() && Lnast_ntype::is_const(lnast->get_type(d2))) {
            const auto kind = lnast->get_name(d2);
            if (kind == "wire") {
              wire_decl.insert(std::string(strip_prefix(lnast->get_name(d0))));
            } else if (kind == "reg" || kind == "latch") {
              state_decl_pre.insert(std::string(strip_prefix(lnast->get_name(d0))));
            }
          }
        }
      };
      scan_wires(scan_wires, stmts_nid);
    }
    // ── Pin-net position-independence ──────────────────────────────────────
    // Decide, per pin net X (a `clock_pin=ref X` / `reset_pin=ref X` target):
    //  - X declared `wire`/`reg`/`latch`, or not driven in this region:
    //    already position-independent — a wire binds any ref to its single
    //    later driver, a flop/latch Q is order-free state, a port is in scope.
    //  - X `mut` with exactly ONE driver — a plain top-level 2-child store and
    //    no declare of its own: pre-declare `wire X` at the region top
    //    (emitted with the other hoists below).  The store stays the wire's
    //    one in-place driver.
    //  - anything else (multi-driven, if-written, set_mask-driven, mut/const
    //    declared, dotted): mint an alias — `wire <X__pinw>` at the region
    //    top, `<X__pinw> = X` at the region END (X's FINAL value; nothing
    //    after can rewrite it), and the folded attr rewritten to
    //    `ref <X__pinw>`.  X itself stays untouched, in body order.
    // Every statement keeps its body position: there is nothing to relocate,
    // no writer-side dependency graph, and no cycle to repair.  A genuinely
    // cyclic gated clock (an enable chain reading Qs of the flops it clocks)
    // is broken where the semantics break it — at the position-independent
    // wire / flop-Q reads.
    // pin_cone_ ends up holding every name the EMITTED attr strings reference
    // (the pin nets plus minted aliases): dead-signal removal and instance-
    // output inlining must not fold a name an attr string spells out.
    std::map<std::string, std::string> pin_alias;  // net -> alias (ordered for deterministic emission)
    absl::flat_hash_set<std::string>   pin_wire_hoist;
    pin_cone_ = pin_dep_nets_;
    if (!pin_dep_nets_.empty()) {
      // Count every definition of a name at ANY nesting depth (an if-arm store
      // or a nested set_mask is a def) and remember each name's single
      // top-level def when there is exactly one.
      absl::flat_hash_map<std::string, int>       def_count;
      absl::flat_hash_map<std::string, Lnast_nid> top_def;
      absl::flat_hash_set<std::string>            nonstate_declared;
      std::function<void(Lnast_nid, bool)>        scan_defs = [&](Lnast_nid n, bool top) {
        for (auto s = lnast->get_child(n); !s.is_invalid(); s = lnast->get_sibling_next(s)) {
          const auto st = lnast->get_type(s);
          if (Lnast_ntype::is_stmts(st) || st == Lnast_ntype::Lnast_ntype_if || st == Lnast_ntype::Lnast_ntype_unique_if) {
            scan_defs(s, false);
            continue;
          }
          auto s0 = lnast->get_child(s);
          if (s0.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(s0)) || !defines_child0(st)) {
            continue;
          }
          auto nm = std::string(strip_prefix(lnast->get_name(s0)));
          if (st == Lnast_ntype::Lnast_ntype_declare) {
            if (wire_decl.count(nm) == 0u && state_decl_pre.count(nm) == 0u) {
              nonstate_declared.insert(nm);  // a mut/const declare (its init may drive)
            }
            continue;
          }
          if (++def_count[nm] == 1 && top) {
            top_def.emplace(nm, s);
          }
        }
      };
      scan_defs(stmts_nid, true);
      for (const auto& nm : pin_dep_nets_) {
        if (wire_decl.count(nm) != 0u || state_decl_pre.count(nm) != 0u || declared_.count(nm) != 0u) {
          continue;  // already position-independent / already in scope
        }
        auto dc = def_count.find(nm);
        if (dc == def_count.end()) {
          continue;  // no in-region driver (an input port, a package name)
        }
        bool plain_single_store = false;
        if (dc->second == 1 && nonstate_declared.count(nm) == 0u && nm.find('.') == std::string::npos) {
          if (auto td = top_def.find(nm); td != top_def.end() && lnast->get_type(td->second) == Lnast_ntype::Lnast_ntype_store) {
            auto s0            = lnast->get_child(td->second);
            auto s1            = s0.is_invalid() ? s0 : lnast->get_sibling_next(s0);
            // Only a plain 2-child store is a single assignment (a set_mask
            // emits a copy plus a masked write — two drivers).
            plain_single_store = !s1.is_invalid() && lnast->is_last_child(s1);
          }
        }
        if (plain_single_store) {
          pin_wire_hoist.insert(nm);
          continue;
        }
        std::string alias = nm + "__pinw";
        std::replace(alias.begin(), alias.end(), '.', '_');
        for (int i = 2; def_count.count(alias) != 0u || declared_.count(alias) != 0u || pin_cone_.count(alias) != 0u
                        || wire_decl.count(alias) != 0u || state_decl_pre.count(alias) != 0u;
             ++i) {
          alias = nm + "__pinw" + std::to_string(i);
          std::replace(alias.begin(), alias.end(), '.', '_');
        }
        // Rewrite every folded `…=ref nm` token (exact-name match) to the alias.
        const std::string from = "=ref " + nm;
        auto              oit  = folded_attr_owners_by_ref_.find(nm);
        if (oit != folded_attr_owners_by_ref_.end()) {
          for (const auto& attr_var : oit->second) {
            auto fit = folded_attrs_.find(attr_var);
            if (fit == folded_attrs_.end()) {
              continue;
            }
            auto& attrs = fit->second;
            for (size_t p = attrs.find(from); p != std::string::npos; p = attrs.find(from, p)) {
              const size_t end = p + from.size();
              if (end == attrs.size() || attrs[end] == ',' || attrs[end] == ' ') {
                attrs.replace(p, from.size(), "=ref " + alias);
                p += 5 + alias.size();
              } else {
                p = end;  // a longer net name that merely starts with nm
              }
            }
          }
        }
        pin_cone_.insert(alias);
        pin_alias.emplace(nm, alias);
      }
    }
    // With the clock/reset cone known, decide which submodule output-port reads
    // collapse to `inst.port` at their uses (vs stay a feedback `wire`).
    analyze_instance_inline();
    // Drop dead signals: nets written but NEVER read anywhere (fold_info use_count
    // == 0 counts every ref — expressions, conditions, instance ports, asserts).
    // firtool's SSA + poison-init split each versioned signal into a live version
    // (`x__w1`) plus a dead base (`mut x = 0; x = 0ub?`); plus dead `_GEN`/probe
    // intermediates. These are pure cruft (LEC compares OUTPUTS, which they never
    // reach). Excluded: io ports, regs/mems, the clock/reset cone, instance temps.
    compute_dead_signals(io_nid, stmts_nid);
    // Collapse mux-shaped if/unique-if into conditional-expression assignments.
    analyze_muxes(stmts_nid);
    // Inline single-use reader temps at their read (_b2i → unsigned(cond),
    // _mux → the conditional expression, __wN SSA copies → their value).
    analyze_expr_inlines(io_nid, stmts_nid);
    // Pre-declare body `mut` vars that are WRITTEN but have no `declare` node.
    // Their first write would otherwise emit `mut X` inside whatever (possibly
    // nested) scope it lands in; a later write in a SIBLING scope then references
    // an out-of-scope X ("assignment to undeclared variable"). Hoisting
    // `mut X = 0` to the function top makes every write in-scope. Vars that DO
    // have a declare node (regs, memories, explicit `mut`) are skipped — they
    // are emitted by their declare (the reg/mem hoist pass below).
    // ── Bundle reconstruction ──────────────────────────────────────────────
    // upass.detuple split a scalar tuple `wire io:(...)` into dotted leaf nets
    // (`io.operation`, `io.inputx`, …). Regroup them into ONE
    // `wire io:(operation:u5, …) = nil` declaration and render each `io.field`
    // access as the bare dotted path (the quote() bundle check above) — so the
    // struct/bundle info surfaces in the emitted Pyrope instead of escaped
    // `` `io.field` `` leaves. On recompile detuple re-splits it. Populate
    // bundle_fields_ + suppress_decl_ BEFORE the hoist scan so strip_prefix is
    // bundle-consistent throughout. Only homogeneous-mode wire/mut leaf sets are
    // bundled (a leaf with a nested dot or a mixed mode leaves its base alone).
    {
      std::vector<std::string>                                                           base_order;
      absl::flat_hash_map<std::string, std::vector<std::pair<std::string, std::string>>> bf;  // base -> [(field,type)]
      absl::flat_hash_map<std::string, std::string>                                      base_mode;
      absl::flat_hash_set<std::string>                                                   base_bad;
      for (auto c = lnast->get_child(stmts_nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
        if (!Lnast_ntype::is_declare(lnast->get_type(c))) {
          continue;
        }
        auto v = lnast->get_child(c);
        if (v.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(v))) {
          continue;
        }
        std::string raw(lnast->get_name(v));  // RAW dotted leaf name (no strip_prefix escaping)
        auto        dot = raw.find('.');
        if (dot == std::string::npos || raw[0] == '%') {
          continue;
        }
        auto        c1   = lnast->get_sibling_next(v);
        auto        c2   = c1.is_invalid() ? c1 : lnast->get_sibling_next(c1);
        std::string mode = (!c2.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c2))) ? std::string(lnast->get_name(c2))
                                                                                            : std::string("mut");
        if (mode != "wire") {
          continue;  // only a plain `wire` leaf bundles — detuple re-splits a `wire`
                     // tuple; a `mut` tuple is left to constprop (kind/overflow checks)
        }
        std::string base  = raw.substr(0, dot);
        std::string field = raw.substr(dot + 1);
        if (field.find('.') != std::string::npos) {
          base_bad.insert(base);  // nested leaf — leave the whole base unbundled
          continue;
        }
        auto bit = bf.find(base);
        if (bit == bf.end()) {
          base_order.push_back(base);
          base_mode[base] = mode;
        } else if (base_mode[base] != mode) {
          base_bad.insert(base);  // mixed wire/mut under one base — unbundle it
        }
        bf[base].emplace_back(field, c1.is_invalid() ? std::string{} : render_type_at(c1));
      }
      // A base WRITTEN WHOLE (`store(base, value)` — a 2-child store of the base
      // itself, e.g. an instance-result struct `_pipeA_if_id_io_data = inst`) is
      // NOT a detuple-split tuple: bundling it would emit `wire base:(...) = nil`
      // over a whole-net driver, which the re-compile rejects (multi-driver). Such
      // a base keeps its flat per-leaf form. Scan the body for whole-base stores.
      std::function<void(Lnast_nid)> scan_whole = [&](Lnast_nid n) {
        for (auto c = lnast->get_child(n); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
          if (Lnast_ntype::is_store(lnast->get_type(c))) {
            auto f0 = lnast->get_child(c);
            if (!f0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(f0))) {
              std::string nm(lnast->get_name(f0));
              auto        f1 = lnast->get_sibling_next(f0);
              // exactly 2 children (ref + value) AND the ref is a bare base name
              if (!f1.is_invalid() && lnast->get_sibling_next(f1).is_invalid() && nm.find('.') == std::string::npos) {
                base_bad.insert(nm);
              }
            }
          }
          scan_whole(c);
        }
      };
      scan_whole(stmts_nid);
      for (const auto& base : base_order) {
        if (base_bad.count(base) || bf[base].empty() || declared_.count(base)) {
          continue;
        }
        // A never-READ bundle base is a write-only / dead tuple (e.g. a Type-C
        // array bundle whose element reads were dead-code-eliminated by cprop).
        // Re-grouping it into `wire base:(...) = nil` emits per-field stores that
        // detuple — which only re-splits a tuple that is READ — leaves as
        // multi-element stores tolg rejects on recompile ("tuple/field store has
        // no hardware lowering"). Leave such a base as flat leaf nets (they
        // recompile as ordinary dead wires and drop out).
        if (!read_field_prefixes_.count(base)) {
          continue;
        }
        print_indent();
        os << base_mode[base] << " " << quote_kw_path(base) << ":(";
        bool first = true;
        for (const auto& [f, t] : bf[base]) {
          if (!first) {
            os << ", ";
          }
          os << quote_kw_path(f);
          if (!t.empty()) {
            os << ":" << t;
          }
          bundle_fields_[base].insert(f);
          first = false;
        }
        os << ") = nil\n";
        declared_.insert(base);
        for (const auto& [f, t] : bf[base]) {
          suppress_decl_.insert(std::string(strip_prefix(base + "." + f)));  // drop the per-leaf declare
        }
      }
    }

    // 1-D declared array sizes (`x:[N]T`, any mode) — write_store expands a
    // whole array-to-array copy (`d = q`) into per-element stores.  Records the
    // ref/type child pair of one `declare`; the nested-declare scan below feeds
    // it too, so an array the reader declared inside an `if` (and that the
    // prologue hoists to the function top) is registered like a top-level one.
    array_decl_size_.clear();
    array_decl_elem_.clear();
    auto record_array_decl_size = [this](Lnast_nid c0, Lnast_nid ty) -> void {
      if (ty.is_invalid() || lnast->get_type(ty) != Lnast_ntype::Lnast_ntype_comp_type_array) {
        return;
      }
      auto elem = lnast->get_child(ty);
      if (elem.is_invalid() || lnast->get_type(elem) == Lnast_ntype::Lnast_ntype_comp_type_array) {
        return;  // multi-dim: element access spelling differs — leave alone
      }
      auto size_n = lnast->get_sibling_next(elem);
      if (size_n.is_invalid()) {
        return;
      }
      std::string sz(lnast->get_name(size_n));  // "[N]"
      if (sz.size() < 3 || sz.front() != '[' || sz.back() != ']') {
        return;
      }
      int64_t n = 0;
      if (auto [p, ec] = std::from_chars(sz.data() + 1, sz.data() + sz.size() - 1, n);
          ec == std::errc() && p == sz.data() + sz.size() - 1 && n > 0) {
        const std::string nm(strip_prefix(lnast->get_name(c0)));
        array_decl_size_[nm] = n;
        // The element's declared WINDOW. `prim_type_int` carries its envelope
        // as (max, min) consts: a negative min means a signed element, whose
        // window is the wider of the two magnitudes; otherwise max's own width
        // less its sign slot. Only a sized integer element qualifies -- an
        // unsized or non-integer one states no window, and a concat window may
        // never be guessed.
        if (lnast->get_type(elem) == Lnast_ntype::Lnast_ntype_prim_type_int) {
          auto mx = lnast->get_child(elem);
          auto mn = mx.is_invalid() ? mx : lnast->get_sibling_next(mx);
          if (!mx.is_invalid() && !mn.is_invalid() && Lnast_ntype::is_const(lnast->get_type(mx))
              && Lnast_ntype::is_const(lnast->get_type(mn))) {
            auto dmax = Dlop::from_pyrope(std::string(lnast->get_name(mx)));
            auto dmin = Dlop::from_pyrope(std::string(lnast->get_name(mn)));
            if (dmax && dmin && dmax->is_integer() && dmin->is_integer() && !dmax->has_unknowns() && !dmin->has_unknowns()) {
              const bool    sgn  = dmin->is_negative();
              const int64_t bits = sgn ? std::max<int64_t>(dmax->get_bits(), dmin->get_bits())
                                       : (dmax->is_known_zero() ? 0 : dmax->get_bits() - 1);
              if (bits > 0) {
                array_decl_elem_[nm] = Array_elem{bits, sgn};
              }
            }
          }
        }
      }
    };
    for (auto c = lnast->get_child(stmts_nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (lnast->get_type(c) != Lnast_ntype::Lnast_ntype_declare) {
        continue;
      }
      auto c0 = lnast->get_child(c);
      if (c0.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(c0))) {
        continue;
      }
      record_array_decl_size(c0, lnast->get_sibling_next(c0));
    }

    {
      absl::flat_hash_set<std::string>              top_decl, nonmut_decl, store_lhs;
      absl::flat_hash_map<std::string, std::string> nested_wire_decl;  // name -> rendered type (or "")
      // The `stmts` that declares each nested wire, and the body order of that
      // declaration.  A wire whose whole reference footprint sits inside its own
      // declaring scope, with nothing mentioning it earlier, needs NO hoist: the
      // in-place declaration is already visible everywhere the net is used, and
      // leaving it there keeps it next to the first use instead of thousands of
      // lines away at the function top (XiangShan's SSIT declares ~4k of these
      // inside one `if`).
      absl::flat_hash_map<std::string, Lnast_nid>   nested_wire_scope;
      absl::flat_hash_map<std::string, uint32_t>    nested_wire_order;
      absl::flat_hash_map<std::string, uint32_t>    first_touch_order;
      uint32_t                                      walk_order = 0;
      // Body order of each name's first WRITE and first READ (a declare's own
      // name is not a read).  A net whose SINGLE write precedes every read needs
      // no position independence, so it can be emitted as a `const X:T = nil`
      // forward declaration instead of a `wire` — same hardware, but the
      // re-parse also gets def-before-use and single-bind checking.
      absl::flat_hash_map<std::string, uint32_t>    first_def_order;
      absl::flat_hash_map<std::string, uint32_t>    first_read_order;
      // Nested `mut` declares, keyed name -> the declaration the hoist must
      // REBUILD.  This MUST carry the type and the initializer, not just the name:
      // the hoisted prologue below re-emits the declaration and `suppress_decl_`
      // deletes the original, so anything not captured here is LOST.  Three ways
      // that bit:
      //   * an ARRAY read at a runtime index whose only use is inside an `if`
      //     (`reg [3:0] mem[1:0]; … if (en) out = mem[sel];`) — the reader declares
      //     it at that first nested use, so the hoist printed `mut mem = 0`, the
      //     `[2]u4` was gone, and the recompile's tolg met `mem[sel]` on a base that
      //     constprop had folded to the scalar 0: "field/index read of '0' could not
      //     be resolved" (a hard error — the emitted Pyrope did not even compile).
      //   * that SAME array losing its CONTENTS.  The reader folds a `initial
      //     mem[0]<=1; mem[1]<=2;` ROM into the declare as a `tuple_add(1,2)`
      //     initializer, and the hardcoded `= 0` threw it away — restoring only the
      //     type would swap the loud tolg error for a SILENT wrong answer (mem reads
      //     0,0 where the golden reads 1,2).  Type and init must land together.
      //   * a nested `_mux_N` temp losing the `:u2`/`:s8` the slang reader stamps to
      //     pin its width/sign (the CLZ priority-encoder divergence its own comment
      //     cites).  Latent, but the same missing byte.
      // A later nested declare of the same name keeps the FIRST non-empty field.
      struct Nested_mut {
        std::string ty;    // rendered type suffix, or "" (untyped)
        std::string init;  // rendered comptime initializer, or "" (seed with 0)
      };
      absl::flat_hash_map<std::string, Nested_mut>                              nested_mut_decl;
      // Definition count per name, over EVERY statement that writes it, in any
      // scope and of any node type (a `store` re-bind, an op node whose child0 is
      // the def — `x = a + b` is a `plus`, not a `store` — a set_mask, a func_call
      // result, …).  A name that is defined exactly once, by a top-level `store`,
      // needs no `mut X = 0` prologue: the store itself can declare it in place.
      // Counting only `store`s would MISS an op-node def and wrongly call a
      // twice-written name single-store (yielding a duplicate declaration).
      absl::flat_hash_map<std::string, int>                                     def_count;
      // Names written from a NESTED scope (inside an if/loop arm). Paired with
      // def_count it identifies a compiler temp the reader's mux lowering writes
      // from sibling arms — see the hoist below.
      absl::flat_hash_set<std::string>                                          nested_def;
      // SCOPE FOOTPRINT per name: the `stmts` node each def / read sits under.
      // A name whose whole footprint fits inside the scope of its FIRST def needs
      // no hoisted `mut X = 0` prologue — that def declares it in place, which
      // also leaves a single-use temp foldable at its use. Only a name reached
      // from a SIBLING (or enclosing) scope needs the function-top seed.
      absl::flat_hash_map<std::string, Lnast_nid>                               first_def_scope;
      absl::flat_hash_map<std::string, absl::flat_hash_map<int64_t, Lnast_nid>> touch_scope;
      auto note_scope = [&](const std::string& nm, Lnast_nid sc, bool is_def) {
        if (is_def && first_def_scope.find(nm) == first_def_scope.end()) {
          first_def_scope.emplace(nm, sc);
        }
        first_touch_order.try_emplace(nm, walk_order);
        touch_scope[nm].try_emplace(sc.get_class_index().value, sc);
      };
      auto scan = [&](auto&& self, Lnast_nid n, bool top) -> void {
        for (auto c = lnast->get_child(n); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
          ++walk_order;  // pre-order body position, for the def-before-use test below
          const auto t     = lnast->get_type(c);
          auto       v     = lnast->get_child(c);
          const bool v_ref = !v.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(v));
          // Only a def whose PARENT is a `stmts` is a real statement write; a
          // `store` under a `func_call` is a named ARGUMENT (`mul(a=in1)`), and a
          // `tuple_add` child is a named field — neither defines a body net.
          if (v_ref && lnast->get_type(n) == Lnast_ntype::Lnast_ntype_stmts && defines_child0(t) && !Lnast_ntype::is_declare(t)) {
            auto dn = std::string(strip_prefix(lnast->get_name(v)));
            ++def_count[dn];
            note_scope(dn, n, /*is_def=*/true);
            first_def_order.try_emplace(dn, walk_order);
            if (!top) {
              nested_def.insert(std::move(dn));
            }
          }
          // Reads of this statement belong to THIS scope (an if/loop's own
          // sub-scopes are visited by the recursion below with their own `n`).
          {
            absl::flat_hash_set<std::string> rr;
            if (Lnast_ntype::is_declare(t)) {
              collect_node_reads(c, rr);
            } else if (v_ref && defines_child0(t)) {
              collect_driver_reads(c, rr);
            } else if (Lnast_ntype::is_stmts(t)) {
              // A nested block owns its own scope: the recursion below notes its
              // reads against the BLOCK. Collecting them here instead pinned an
              // if-ARM's whole body on the `if` NODE — a scope no declaration
              // can be written in, so every name used inside any `if` looked
              // un-contained and had to be hoisted.
            } else if (t != Lnast_ntype::Lnast_ntype_if && t != Lnast_ntype::Lnast_ntype_unique_if) {
              collect_node_reads(c, rr);
            } else if (auto cond = lnast->get_child(c); !cond.is_invalid()) {
              collect_node_reads(cond, rr);  // an if's CONDITION only; arms recurse
            }
            const std::string self_decl
                = (v_ref && Lnast_ntype::is_declare(t)) ? std::string(strip_prefix(lnast->get_name(v))) : std::string{};
            for (const auto& r : rr) {
              note_scope(r, n, /*is_def=*/false);
              if (r != self_decl) {
                first_read_order.try_emplace(r, walk_order);
              }
            }
          }
          if (v_ref && Lnast_ntype::is_declare(t)) {
            auto        nm     = std::string(strip_prefix(lnast->get_name(v)));
            auto        c1     = lnast->get_sibling_next(v);
            auto        c2     = c1.is_invalid() ? c1 : lnast->get_sibling_next(c1);
            std::string mode   = (!c2.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c2))) ? std::string(lnast->get_name(c2))
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
                nested_wire_scope.emplace(nm, n);
                nested_wire_order.emplace(nm, walk_order);
              }
            } else if (!top) {
              // declare( ref, type, const(qualifier), [init] ) — same child walk as
              // write_declare, whose emission this hoist replaces.
              auto        ty = c1.is_invalid() ? std::string{} : render_type_at(c1);
              auto        c3 = c2.is_invalid() ? c2 : lnast->get_sibling_next(c2);
              std::string init;
              if (is_comptime_init(c3)) {
                init = render_comptime_init(c3);
              }
              auto [it, fresh] = nested_mut_decl.try_emplace(nm, Nested_mut{ty, init});
              if (!fresh) {  // first non-empty wins, per field
                if (it->second.ty.empty()) {
                  it->second.ty = std::move(ty);
                }
                if (it->second.init.empty()) {
                  it->second.init = std::move(init);
                }
              }
              record_array_decl_size(v, c1);  // the hoist makes it a top-level array
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
          if (!Lnast_ntype::is_declare(t)) {
            // A declare's children are its own name / type / mode — the block
            // above already read them. Descending re-collects the declared name
            // as a READ of itself, which made every name look read before its
            // own def.
            self(self, c, false);
          }
        }
      };
      scan(scan, stmts_nid, true);
      def_count_ = def_count;  // write_declare needs it (a stored `const` declare is a `mut`)
      // Position every top-level statement in EMIT order, so a name's single store
      // can be checked to precede every read of it.  Body emit order is: the
      // hoisted prologue (mut seeds + wire pre-declares, incl. the pin wires),
      // then ALL declares (pass 0), then the non-declares in body order
      // (pass 1), then the pin-alias assignments.  A read inside a *declare*
      // lands ahead of EVERY non-declare store — such a name must keep its
      // hoist, or the in-place declaration would come after its first read
      // (Pyrope rejects a read of an undeclared name).
      absl::flat_hash_map<std::string, size_t> store_pos;   // name -> body index of its top-level store
      absl::flat_hash_map<std::string, size_t> first_read;  // name -> earliest non-declare body index reading it
      absl::flat_hash_set<std::string>         decl_read;   // read by a declare — emitted before every store
      // %tmps whose value IS an imported-package comptime const (`%t = tuple_get
      // (pkg, PARAM)`, or a tmp copy of such a tmp).  A store whose RHS ref is one
      // of these is a pkg-valued store exactly like a bare `pkg.PARAM` RHS.  Tmps
      // are defined before their reads in body order, so one forward pass suffices.
      absl::flat_hash_set<std::string>         pkg_valued_tmp;
      {
        size_t idx = 0;
        for (auto c = lnast->get_child(stmts_nid); !c.is_invalid(); c = lnast->get_sibling_next(c), ++idx) {
          const auto ct = lnast->get_type(c);
          auto       c0 = lnast->get_child(c);
          if (ct == Lnast_ntype::Lnast_ntype_tuple_get && !c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0))
              && is_tmp(lnast->get_name(c0))) {
            auto base = lnast->get_sibling_next(c0);
            if (!base.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(base))
                && is_imported_package_name(strip_prefix(lnast->get_name(base)))) {
              auto idx0 = lnast->get_sibling_next(base);
              if (!idx0.is_invalid() && lnast->get_sibling_next(idx0).is_invalid()
                  && lnast->get_type(idx0) == Lnast_ntype::Lnast_ntype_const) {
                pkg_valued_tmp.insert(std::string(strip_prefix(lnast->get_name(c0))));
              }
            }
          }
          if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0)) && defines_child0(ct) && !Lnast_ntype::is_declare(ct)) {
            def_idx_.emplace(std::string(strip_prefix(lnast->get_name(c0))), idx);  // FIRST wins
          }
          if (Lnast_ntype::is_store(ct) && !c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0))) {
            // Only a SCALAR store (exactly 2 children: the ref and the value) can
            // carry the declaration.  A store with index levels emits `x[0] = v`
            // (write_store), and `const x[0] = v` is not a legal declaration — such
            // a name keeps its hoist.
            auto val = lnast->get_sibling_next(c0);
            if (!val.is_invalid() && lnast->is_last_child(val)) {
              auto nm = std::string(strip_prefix(lnast->get_name(c0)));
              store_pos.emplace(nm, idx);
              // RHS resolves to an imported-package comptime const — a bare
              // `pkg.PARAM` ref, or a %tmp holding one: this single-store net must
              // emit `mut`, not `const` (see the set's doc — a comptime `const`
              // copied into a mux target trips a rebind on recompile).
              if (Lnast_ntype::is_ref(lnast->get_type(val))) {
                auto vn = strip_prefix(lnast->get_name(val));
                if (is_imported_pkg_path(vn) || pkg_valued_tmp.count(std::string(vn))) {
                  if (is_tmp(lnast->get_name(c0))) {
                    pkg_valued_tmp.insert(nm);  // tmp-to-tmp copy stays in the tmp set
                  } else {
                    pkg_valued_store_.insert(nm);
                  }
                }
              }
            }
          }
          absl::flat_hash_set<std::string> reads;
          if (ct == Lnast_ntype::Lnast_ntype_if || ct == Lnast_ntype::Lnast_ntype_unique_if) {
            collect_if_reads(collect_if_reads, c, reads);
          } else if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0)) && defines_child0(ct)) {
            collect_driver_reads(c, reads);  // excludes child0 (the written lhs)
          } else {
            collect_node_reads(c, reads);
          }
          for (const auto& r : reads) {
            if (Lnast_ntype::is_declare(ct)) {
              decl_read.insert(r);
            } else if (auto it = first_read.find(r); it == first_read.end()) {
              read_idx_.emplace(r, idx);
              first_read.emplace(r, idx);
            } else if (idx < it->second) {
              it->second = idx;
            }
          }
        }
      }
      // A store-driven net needs NO hoist when it is defined exactly once, by a
      // top-level store, with no earlier read: the store declares it in place as
      // `const X = <rhs>` (decl_prefix).  This is the overwhelmingly common shape
      // in reader output — a firtool/slang SSA net assigned once and read later —
      // and hoisting it cost a prologue line plus a dead `= 0` store for every one
      // (94.6% of the 249k hoists in XSCore's Rob).
      auto no_hoist_needed = [&](const std::string& nm) {
        if (def_count[nm] != 1) {
          return false;  // written more than once (or by a non-store def) — needs the seed
        }
        auto sp = store_pos.find(nm);
        if (sp == store_pos.end()) {
          return false;  // its lone def is nested, not a top-level store
        }
        if (decl_read.count(nm) || folded_attr_refs_.count(nm)) {
          return false;  // read by a declare (or by an attr folded ONTO one) — declares emit first
        }
        auto fr = first_read.find(nm);
        // `<=` (not `<`) also rejects a self-referencing store (`X = X + 1`), whose
        // RHS reads the value the hoisted `0` seeds.
        return fr == first_read.end() || fr->second > sp->second;
      };
      // A combinational `mut` var written/declared in a nested scope but used in
      // SIBLING scopes must be declared at the function top (its first write
      // otherwise emits `mut` inside one scope, leaving sibling writes out of
      // scope). Hoist `mut X[:T] = 0` for: store-driven vars with no declare, and
      // vars with a NESTED `mut` declare. Skip top-declared / io / reg|latch|const.
      // The map's value is the declaration to rebuild: empty for a store-driven var
      // (no declare to take a type/init from — its width comes from the store's
      // RHS, and `0` is just a seed), the nested declare's own type/init otherwise.
      // True when `nm`'s whole def/read footprint fits inside `anchor`, i.e. a
      // declaration placed in `anchor` is visible everywhere the name is used.
      // Walks parents because a nested scope is still covered by an enclosing
      // one (`stmts` nest inside `if` inside `stmts`).
      auto scope_covers = [&](Lnast_nid anchor, const std::string& nm) {
        auto tit = touch_scope.find(nm);
        if (anchor.is_invalid() || tit == touch_scope.end()) {
          return false;
        }
        for (const auto& [scope_id, sc] : tit->second) {
          (void)scope_id;
          bool covered = false;
          for (auto up = sc; !up.is_invalid(); up = lnast->get_parent(up)) {
            if (up == anchor) {
              covered = true;
              break;
            }
          }
          if (!covered) {
            return false;
          }
        }
        return true;
      };
      // The same test anchored at the scope of `nm`'s FIRST def — declaring it
      // at that def is then visible everywhere it is used.
      auto scope_contained = [&](const std::string& nm) {
        auto fit = first_def_scope.find(nm);
        if (fit == first_def_scope.end()) {
          return false;  // no def seen: keep the conservative hoist
        }
        return scope_covers(fit->second, nm);
      };
      absl::flat_hash_map<std::string, Nested_mut> need;
      for (const auto& nm : store_lhs) {
        if (bool_inline_.count(nm) != 0u || value_inline_.count(nm) != 0u) {
          continue;  // inlined at their single read — no declaration ever emits
        }
        if (!top_decl.count(nm) && !nonmut_decl.count(nm) && !declared_.count(nm) && !pin_wire_hoist.count(nm)
            && !instance_output_inlined_.count(nm) && !dead_signals_.count(nm)) {
          if (no_hoist_needed(nm)) {
            single_store_.insert(nm);  // declared in place by its store, as `const X = <rhs>`
            continue;
          }
          need.try_emplace(nm);  // pin-wire-hoisted nets get a bare `wire` pre-declare instead (below)
        }
      }
      for (const auto& [nm, decl] : nested_mut_decl) {
        if (bool_inline_.count(nm) != 0u || value_inline_.count(nm) != 0u) {
          suppress_decl_.insert(nm);  // inlined at its single read — no hoist, no in-place declare
          continue;
        }
        if (!top_decl.count(nm) && !nonmut_decl.count(nm) && !declared_.count(nm) && !instance_output_inlined_.count(nm)
            && !dead_signals_.count(nm)) {
          // A COMPILER TEMP whose def and every read live inside one scope needs
          // no prologue: its def declares it in place (`const t = a & 1`), which
          // also keeps a single-use temp inlinable at its use. Hoisting it
          // instead emitted a dead `mut t = 0` seed AND blocked the fold.
          // `decl.init == "0"` is a SEED, not content: the name has a real def
          // (def_count == 1) that overwrites it, and a compiler temp is
          // def-before-use by construction. A typed declare still hoists — the
          // in-place `const t = …` the def mints carries no width pin.
          if (is_tmp(nm) && def_count[nm] == 1 && decl.ty.empty() && (decl.init.empty() || decl.init == "0")
              && scope_contained(nm)) {
            suppress_decl_.insert(nm);  // drop the declare; the def mints the keyword
            continue;
          }
          need[nm] = decl;            // the declare's type/init outrank a store-driven empty
          suppress_decl_.insert(nm);  // its in-place nested `mut` declare is dropped
        }
      }
      // A compiler temp is normally single-assignment, so decl_prefix declares it
      // in place at its first write (`const t_x = …`). One written from SIBLING
      // scopes — the reader's mux lowering, `if c { t = a } else { t = b }` —
      // breaks that: the `const` lands inside the first arm, and the other arm's
      // write plus every later read are then out of scope ("assignment to
      // undeclared variable 't_mux_31_0'" on recompile). Seed those at the
      // function top like any other cross-scope `mut`. Single-def temps keep the
      // in-place `const` (the overwhelmingly common shape).
      for (const auto& [nm, cnt] : def_count) {
        if (cnt < 2 || !is_tmp(nm)) {
          continue;
        }
        if (bool_inline_.count(nm) != 0u || value_inline_.count(nm) != 0u || declared_.count(nm) != 0u || top_decl.count(nm) != 0u
            || nonmut_decl.count(nm) != 0u) {
          continue;
        }
        // Written more than once, so decl_prefix must not call it `const` — a
        // `const t = v` followed by a lane write `t#[1..=1] = w` is a rebind
        // ("const `t` rebind (assigned 2 times)"). A same-scope multi-def keeps
        // its in-place declaration (now `mut`); one written across SIBLING
        // scopes also needs the top-level seed.
        multi_def_tmp_.insert(nm);
        if (nested_def.count(nm) != 0u) {
          need.try_emplace(nm);
        }
      }
      std::vector<std::string> pre;
      pre.reserve(need.size());
      for (const auto& [nm, decl] : need) {
        (void)decl;
        pre.push_back(nm);
      }
      std::sort(pre.begin(), pre.end());
      for (const auto& nm : pre) {
        const auto& decl = need[nm];
        print_indent();
        os << "mut " << nm;
        if (!decl.ty.empty()) {
          os << ":" << decl.ty;
        }
        os << " = " << (decl.init.empty() ? std::string("0") : decl.init) << "\n";
        declared_.insert(nm);
      }
      // Hoist nested `wire` declares to the function top as a bare `wire X:T`
      // (no `= 0` — the body store is the wire's single, position-independent
      // driver; a default init would make it multi-driven).  The in-place nested
      // declare is dropped via suppress_decl_.
      //
      // EXCEPT when the declaring scope already covers the net's whole footprint
      // AND nothing mentions the name before the declaration: then the in-place
      // declaration is legal Pyrope on its own, so keep it and give the reader a
      // declaration next to the first use.  (A wire declared inside an `if` and
      // used only there is the Verilog reader's shape for a procedural
      // `automatic`; hoisting those to the top separated 4096 of SSIT's 4101
      // wire declarations from their uses by ~24k lines.)
      // A `wire` whose SINGLE write precedes every read is emitted as a
      // `const X:T = nil` forward declaration: a `const` is bound exactly once
      // and its bind — conditional or not — defines the net on every path, so
      // the hardware is identical, and saying `const` states the intent while
      // buying def-before-use checking on re-parse.  A net a declare or a folded
      // clock/reset attr reads keeps `wire` (those emit ahead of the body), and
      // so does one read before its write — that is what position independence
      // is FOR.
      auto const_nil_ok = [&](const std::string& nm) {
        auto dc = def_count.find(nm);
        if (dc == def_count.end() || dc->second != 1 || pin_cone_.count(nm) != 0u || folded_attr_refs_.count(nm) != 0u
            || decl_read.count(nm) != 0u) {
          return false;
        }
        auto di = first_def_order.find(nm);
        if (di == first_def_order.end()) {
          return false;
        }
        auto ri = first_read_order.find(nm);
        return ri == first_read_order.end() || di->second < ri->second;
      };
      for (const auto& [nm, ty] : nested_wire_decl) {
        (void)ty;
        if (const_nil_ok(nm)) {
          const_nil_wire_.insert(nm);
        }
      }
      std::vector<std::string> wpre;
      for (const auto& [nm, ty] : nested_wire_decl) {
        if (!top_decl.count(nm) && !declared_.count(nm) && !instance_output_inlined_.count(nm)) {
          // A net a DECLARE reads — a `clock_pin=ref X` / `reset_pin=ref X`
          // folded onto a reg, or any other read from a declare — must stay at
          // the top: every declare emits before every body statement, so a
          // declaration sunk into the body would come after its reader.  The
          // scope test cannot see a FOLDED attr (it is a string by then), which
          // is why pin_cone_/folded_attr_refs_/decl_read are checked by name.
          auto oit = nested_wire_order.find(nm);
          auto fit = first_touch_order.find(nm);
          if (oit != nested_wire_order.end() && fit != first_touch_order.end() && oit->second <= fit->second
              && pin_cone_.count(nm) == 0u && folded_attr_refs_.count(nm) == 0u && decl_read.count(nm) == 0u
              && scope_covers(nested_wire_scope[nm], nm)) {
            continue;  // declared where it is used — no hoist, keep the in-place declare
          }
          wpre.push_back(nm);
        }
      }
      std::sort(wpre.begin(), wpre.end());
      for (const auto& nm : wpre) {
        print_indent();
        const bool as_const = const_nil_wire_.count(nm) != 0u;
        os << (as_const ? "const " : "wire ") << nm;
        if (const auto& ty = nested_wire_decl[nm]; !ty.empty()) {
          os << ":" << ty;
        }
        if (as_const) {
          os << " = nil";
        }
        os << "\n";
        declared_.insert(nm);
        suppress_decl_.insert(nm);
      }
      // Pin nets promoted to `wire`, plus the minted pin aliases: bare wire
      // pre-declares at the function top.  Untyped — tolg restamps the buffer
      // width from the single driver (the body store / the end-of-body alias
      // assignment).  With the wire declared, decl_prefix leaves the body
      // store a plain re-assignment instead of minting `mut X = <driver>`.
      {
        std::vector<std::string> pnw(pin_wire_hoist.begin(), pin_wire_hoist.end());
        for (const auto& [nm, alias] : pin_alias) {
          (void)nm;
          pnw.push_back(alias);
        }
        std::sort(pnw.begin(), pnw.end());
        for (const auto& nm : pnw) {
          if (declared_.count(nm) != 0u) {
            continue;
          }
          print_indent();
          os << "wire " << nm << "\n";
          declared_.insert(nm);
        }
      }
      // Any `wire` that has a real-statement store driver must not receive
      // write_declare's combinational `= 0` default (it is single-driver).
      wire_stored_.insert(store_lhs.begin(), store_lhs.end());
    }
    // Emit top-level `declare` statements first.  The slang reader places an
    // `attr_set` (e.g. `data.[fwd]=0`, a reg's `reset_pin`/`sync`/`initial`)
    // *before* the reg/memory `declare` it qualifies; Pyrope rejects an
    // attribute write to an undeclared variable, so hoisting the declares above
    // those attr writes makes the output reparse.  These declares carry no
    // forward-referencing init (the slang reset value is the `nil` sentinel,
    // suppressed), so reordering is semantically inert; the non-declare
    // statements keep their original order.
    //
    // Order WITHIN the declare pass: a reg/latch whose Q drives another declare's
    // `clock_pin=ref X` / `reset_pin=ref X` must be DECLARED FIRST.  A divided
    // clock (`always_ff @(posedge div_q)`) otherwise emitted
    // `reg data_o:…:[clock_pin=ref word_clk]` ABOVE `reg word_clk`, and the
    // re-read failed with "read of undefined variable 'word_clk'".  The pin
    // machinery above makes such a net position-independent for TOLG (a flop Q is
    // order-free state), but Pyrope's scope check is textual, so the declaration
    // still has to precede the reference.
    // depth(X) = 0 when X's own declare references no other pin-referenced state
    // net, else 1 + the max depth of the ones it does (a clock derived from a
    // divided clock).  Declares go out lowest-depth first.
    absl::flat_hash_map<std::string, int> pin_state_depth;
    absl::flat_hash_set<std::string>      pin_state_names;
    for (const auto& nm : pin_dep_nets_) {
      if (state_decl_pre.count(nm) != 0u) {
        pin_state_names.insert(nm);
      }
    }
    absl::flat_hash_set<std::string>       visiting;
    std::function<int(const std::string&)> pin_depth = [&](const std::string& nm) -> int {
      if (auto it = pin_state_depth.find(nm); it != pin_state_depth.end()) {
        return it->second;
      }
      if (!visiting.insert(nm).second) {
        return 0;  // malformed dependency cycle: keep deterministic source order
      }
      int dep_depth = 0;
      if (auto refs = folded_attr_refs_by_owner_.find(nm); refs != folded_attr_refs_by_owner_.end()) {
        for (const auto& other : refs->second) {
          if (other != nm && pin_state_names.contains(other)) {
            dep_depth = std::max(dep_depth, pin_depth(other) + 1);
          }
        }
      }
      visiting.erase(nm);
      pin_state_depth.emplace(nm, dep_depth);
      return dep_depth;
    };
    int max_pin_depth = 0;
    for (const auto& nm : pin_state_names) {
      max_pin_depth = std::max(max_pin_depth, pin_depth(nm));
    }
    const int decl_pass = pin_state_depth.empty() ? 0 : max_pin_depth + 1;
    struct Ordered_stmt {
      Lnast_nid nid;
      int       order;
    };
    std::vector<Ordered_stmt> decls;
    std::vector<Lnast_nid>    body;
    for (auto stmt = lnast->get_child(stmts_nid); !stmt.is_invalid(); stmt = lnast->get_sibling_next(stmt)) {
      if (lnast->get_type(stmt) != Lnast_ntype::Lnast_ntype_declare) {
        body.push_back(stmt);
        continue;
      }
      int order = decl_pass;
      if (auto d0 = lnast->get_child(stmt); !d0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(d0))) {
        if (auto pit = pin_state_depth.find(std::string(strip_prefix(lnast->get_name(d0)))); pit != pin_state_depth.end()) {
          order = pit->second;
        }
      }
      decls.push_back({stmt, order});
    }
    std::stable_sort(decls.begin(), decls.end(), [](const Ordered_stmt& lhs, const Ordered_stmt& rhs) {
      return lhs.order < rhs.order;
    });

    std::vector<Lnast_nid> ordered;
    ordered.reserve(decls.size() + body.size());
    for (const auto& decl : decls) {
      ordered.push_back(decl.nid);
    }
    ordered.insert(ordered.end(), body.begin(), body.end());

    cur = stmts_nid;
    if (move_to_child()) {  // establish the cursor's parent stack once
      for (const auto& stmt : ordered) {
        cur = stmt;
        if (is_folded_node(cur) || emits_nothing_stmt(cur)) {
          continue;
        }
        if (current_ntype() == Lnast_ntype::Lnast_ntype_attr_set) {
          auto tgt = lnast->get_child(cur);
          if (!tgt.is_invalid() && folded_attrs_.count(std::string(strip_prefix(lnast->get_name(tgt))))) {
            continue;
          }
        }
        // As in write_stmts: a statement that decides to emit nothing only once
        // it is being written must not leave a whitespace-only line behind.
        const auto before = os.tellp();
        print_indent();
        const auto after_indent = os.tellp();
        write_node();
        if (before != std::streampos(-1) && os.tellp() == after_indent) {
          os.seekp(before);
          continue;
        }
        os << "\n";
      }
      move_to_parent();  // cur -> stmts, pop
    }
    // Pin aliases: each alias wire's single driver, at the region END so it
    // reads the pin net's FINAL value (nothing after can rewrite it) — the
    // same value the reg declare's old relocated `ref <net>` bound, with no
    // statement moved to get it.
    for (const auto& [nm, alias] : pin_alias) {
      print_indent();
      os << alias << " = " << nm << "\n";
    }
  }

  --depth;
  print_indent();
  print("}\n");
  cur = io_nid;  // restore for the caller's move_to_parent()
}

// ── io tuple-port regrouping ────────────────────────────────────────────────
// One interior/leaf node of a reconstructed tuple port. Kids are kept in
// first-appearance order; leaves land exactly in io-declaration order (the
// recompile's bit-packing correspondence depends on it).
namespace {
struct Port_group_node {
  std::vector<std::pair<std::string, std::unique_ptr<Port_group_node>>> kids;
  std::string                                                           type_text;  // leaf only ("" = untyped)
  bool                                                                  is_leaf = false;
};
}  // namespace

// upass.ssa flattens a tuple-typed port (`d:(x:u3, y:u5)`) into dotted leaf io
// entries — printed per-leaf they become opaque escaped ports (`` `d.x`:u3 ``)
// and the tuple structure is lost. This pre-header scan regroups them so the
// header prints `d:(x:u3, y:u5)` (multi-level leaves re-nest:
// `req:(hdr:(a:u1, b:u2), pay:u4)`) and the body prints bare dotted accesses;
// the emitted .prp recompiles to the IDENTICAL per-leaf interface. A base is
// NOT regrouped (keeps today's per-leaf printing) when: a bare port of the
// same spelling exists, the base has entries in BOTH directions, a leaf is a
// vararg / escaped-id / SSA-versioned name, the leaf paths conflict (`a.b`
// both leaf and interior), or (mod outputs) the leaves carry DIFFERENT
// landing-cycle stages — the single `@[…]` after the group must speak for
// every leaf.
void Lnast_prp_writer::collect_port_groups(Lnast_nid io_nid, bool is_mod) {
  port_group_text_.clear();
  port_group_skip_.clear();
  if (io_nid.is_invalid()) {
    return;
  }
  auto in_tup  = lnast->get_child(io_nid);
  auto out_tup = in_tup.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(in_tup);

  // Slang can preserve a packed-struct port as a nested tuple type instead of
  // the already-flattened `base.field` IO entries handled below. Register the
  // same bundle-field index for that representation so body reads print as
  // real Pyrope paths (`din.fp`) rather than opaque escaped identifiers
  // (`` `din.fp` ``). The latter is a different, undeclared variable after a
  // round trip. render_type_at() serializes the nested signature itself.
  auto register_nested = [&](auto&& self, Lnast_nid tuple, const std::string& base, const std::string& prefix) -> void {
    for (auto field = lnast->get_child(tuple); !field.is_invalid(); field = lnast->get_sibling_next(field)) {
      if (!Lnast_ntype::is_store(lnast->get_type(field))) {
        continue;
      }
      auto name_nid = lnast->get_child(field);
      if (name_nid.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(name_nid))) {
        continue;
      }
      const std::string leaf(lnast->get_name(name_nid));
      const std::string path = prefix.empty() ? leaf : prefix + "." + leaf;
      bundle_fields_[base].insert(path);
      declared_.insert(base + "." + path);
      auto init_nid = lnast->get_sibling_next(name_nid);
      auto type_nid = init_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(init_nid);
      if (!type_nid.is_invalid() && Lnast_ntype::is_tuple_add(lnast->get_type(type_nid))) {
        self(self, type_nid, base, path);
      }
    }
  };
  for (const auto& tuple : {in_tup, out_tup}) {
    if (tuple.is_invalid()) {
      continue;
    }
    for (auto port = lnast->get_child(tuple); !port.is_invalid(); port = lnast->get_sibling_next(port)) {
      auto name_nid = lnast->get_child(port);
      if (name_nid.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(name_nid))) {
        continue;
      }
      auto init_nid = lnast->get_sibling_next(name_nid);
      auto type_nid = init_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(init_nid);
      if (type_nid.is_invalid() || !Lnast_ntype::is_tuple_add(lnast->get_type(type_nid))) {
        continue;
      }
      const std::string base(lnast->get_name(name_nid));
      declared_.insert(base);
      register_nested(register_nested, type_nid, base, {});
    }
  }

  // Not a candidate leaf: an escaped Verilog id (`` `ar.x` `` — its dot is NOT
  // a field separator), a `%` temp, or an SSA-versioned name (strip_prefix
  // renames it; the io list should never carry one, but keep it per-leaf).
  auto opaque = [](std::string_view raw) {
    return raw.empty() || raw.front() == '`' || raw.front() == '%' || raw.find("___ssa_") != std::string_view::npos;
  };

  // Pass 1 (both directions): bare port names, plus each dotted base's
  // direction mask — a base that also names a bare port (regrouping would
  // mint a colliding second `base` entry) or straddles in/out is vetoed.
  absl::flat_hash_set<std::string>      bare;
  absl::flat_hash_map<std::string, int> base_dir;  // 1 = input, 2 = output
  auto                                  prescan = [&](Lnast_nid tup, int dir) {
    if (tup.is_invalid()) {
      return;
    }
    for (auto port = lnast->get_child(tup); !port.is_invalid(); port = lnast->get_sibling_next(port)) {
      auto name_nid = lnast->get_child(port);
      if (name_nid.is_invalid()) {
        continue;
      }
      std::string raw(lnast->get_name(name_nid));
      auto        dot = raw.find('.');
      if (dot == std::string::npos || opaque(raw)) {
        bare.insert(raw);
        continue;
      }
      base_dir[raw.substr(0, dot)] |= dir;
    }
  };
  prescan(in_tup, 1);
  prescan(out_tup, 2);

  auto harvest = [&](Lnast_nid tup, bool is_output) {
    if (tup.is_invalid()) {
      return;
    }
    struct Group {
      Port_group_node                              root;
      std::vector<std::pair<std::string, int64_t>> leaves;  // (full raw leaf name, io-store nid) in io order
      std::string                                  stages;  // formatted landing-cycle body (mod outputs)
      bool                                         stages_set = false;
      bool                                         bad        = false;
    };
    std::vector<std::string>                order;  // bases by first appearance
    absl::flat_hash_map<std::string, Group> groups;
    for (auto port = lnast->get_child(tup); !port.is_invalid(); port = lnast->get_sibling_next(port)) {
      auto name_nid = lnast->get_child(port);
      if (name_nid.is_invalid()) {
        continue;
      }
      std::string raw(lnast->get_name(name_nid));
      auto        dot = raw.find('.');
      if (dot == std::string::npos || opaque(raw)) {
        continue;
      }
      std::string base = raw.substr(0, dot);
      if (bare.count(base) != 0u || base_dir[base] == 3) {
        continue;  // vetoed — every leaf of this base keeps the per-leaf printing
      }
      auto git = groups.find(base);
      if (git == groups.end()) {
        order.push_back(base);
        git = groups.emplace(base, Group{}).first;
      }
      Group& g        = git->second;
      auto   init_nid = lnast->get_sibling_next(name_nid);
      auto   type_nid = init_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(init_nid);
      if (!type_nid.is_invalid() && Lnast_ntype::is_stages(lnast->get_type(type_nid))) {
        type_nid = Lnast_nid{};  // trailing stages, not the type slot
      }
      // A var-arg leaf (`...` init) has no tuple-field spelling.
      if (!init_nid.is_invalid() && lnast->get_type(init_nid) == Lnast_ntype::Lnast_ntype_const
          && lnast->get_name(init_nid) == "...") {
        g.bad = true;
      }
      // One `@[…]` prints for the WHOLE group, so every leaf must agree. A
      // stage-less leaf and the bare-pipe sentinel both format to "" (`@[]`),
      // so compare the formatted body. Inputs / comb outputs never print
      // stages — no constraint there.
      if (is_mod && is_output) {
        auto        st  = find_stages_child(port);
        std::string stx = st.is_invalid() ? std::string{} : format_stages(st);
        if (!g.stages_set) {
          g.stages     = stx;
          g.stages_set = true;
        } else if (g.stages != stx) {
          g.bad = true;
        }
      }
      // Field type: the per-leaf io_type_names_ override (keyed by the FULL
      // leaf name, e.g. `req_data_i.paddr` — a package-param dim alias) wins
      // over render_type_at, exactly as for a scalar port.
      std::string ty;
      if (auto ait = lnast->get_io_type_names().find(raw); ait != lnast->get_io_type_names().end()) {
        ty = ait->second;
      } else if (!type_nid.is_invalid()) {
        ty = render_type_at(type_nid);
      }
      // Grow the nested tuple along the leaf's successive path segments.
      Port_group_node* node  = &g.root;
      std::string_view rest  = std::string_view(raw).substr(dot + 1);
      size_t           start = 0;
      while (!g.bad) {
        auto        seg_end = rest.find('.', start);
        const bool  last    = seg_end == std::string_view::npos;
        std::string comp(rest.substr(start, last ? std::string_view::npos : seg_end - start));
        if (comp.empty()) {
          g.bad = true;
          break;
        }
        Port_group_node* kid = nullptr;
        for (auto& [nm, k] : node->kids) {
          if (nm == comp) {
            kid = k.get();
            break;
          }
        }
        if (kid == nullptr) {
          node->kids.emplace_back(comp, std::make_unique<Port_group_node>());
          kid          = node->kids.back().second.get();
          kid->is_leaf = last;
          if (last) {
            kid->type_text = ty;
          }
        } else if (last || kid->is_leaf) {
          g.bad = true;  // duplicate leaf, or a path both leaf and interior
          break;
        }
        if (last) {
          break;
        }
        node  = kid;
        start = seg_end + 1;
      }
      g.leaves.emplace_back(raw, port.get_class_index().value);
    }
    // Commit the good groups: render the header text, mark the later leaves
    // skipped, and mirror the wire-bundle regroup's bookkeeping so body
    // accesses print bare dotted and nothing re-declares (or hoists) a leaf.
    for (const auto& base : order) {
      Group& g = groups[base];
      if (g.bad || g.leaves.empty()) {
        continue;
      }
      auto render = [&](auto&& self, const Port_group_node& n) -> std::string {
        if (n.is_leaf) {
          return n.type_text.empty() ? std::string{} : ":" + n.type_text;
        }
        std::string out = ":(";
        bool        f   = true;
        for (const auto& [nm, kid] : n.kids) {
          if (!f) {
            out += ", ";
          }
          out += quote_kw_path(nm) + self(self, *kid);
          f    = false;
        }
        return out + ")";
      };
      std::string text = quote_kw_path(base) + render(render, g.root);
      if (is_mod && is_output) {
        text += std::format("@[{}]", g.stages);
      }
      port_group_text_.emplace(g.leaves.front().second, std::move(text));
      for (size_t i = 1; i < g.leaves.size(); ++i) {
        port_group_skip_.insert(g.leaves[i].second);
      }
      declared_.insert(base);
      for (const auto& [raw_leaf, key] : g.leaves) {
        (void)key;
        auto rest = raw_leaf.substr(raw_leaf.find('.') + 1);
        bundle_fields_[base].insert(rest);
        // decl_prefix looks the strip_prefix'd (per-component keyword-escaped)
        // path up, and is_bundle_field only unquotes a WHOLE-remainder escape —
        // also key the escaped spelling so a keyword interior segment
        // (`req.in.x` -> `` req.`in`.x ``) still hits.
        if (auto q = quote_kw_path(rest); q != rest) {
          bundle_fields_[base].insert(q);
        }
        declared_.insert(raw_leaf);
        declared_.insert(std::string(strip_prefix(raw_leaf)));
      }
    }
  };
  harvest(in_tup, /*is_output=*/false);
  harvest(out_tup, /*is_output=*/true);
}

// Re-nest a struct LOCAL the front end flattened into per-field leaves. The
// slang reader splits a packed-struct var into one net per field and spells each
// as ONE escaped identifier (`` `sigs_qual_exa_h.cmd` ``), so the body reads
//
//   mut `sigs_qual_exa_h.cmd` = 0
//   mut `sigs_qual_exa_h.txfma` = 0        (x35)
//   `sigs_qual_exa_h.ldst` = …
//
// where the source had one struct. Rebuild the bundle at emit time:
//
//   mut sigs_qual_exa_h = (mut cmd = 0, mut txfma = 0, …)
//   sigs_qual_exa_h.ldst = …
//
// The dot inside an escaped id is NOT normally a field separator (a Verilog
// `\a.b ` is one name), so the regroup is deliberately conservative — it fires
// only on a base with SEVERAL leaves, every one of them a plain identifier
// path, none of them state, and the base never used as a bare name. Under those
// conditions the rewrite only renames private combinational locals, which is
// exactly the flattening this undoes.
void Lnast_prp_writer::collect_body_bundles(Lnast_nid body_nid) {
  if (body_nid.is_invalid()) {
    return;
  }
  auto plain_ident = [](std::string_view s) {
    if (s.empty() || (std::isalpha(static_cast<unsigned char>(s.front())) == 0 && s.front() != '_')) {
      return false;
    }
    for (char c : s) {
      if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
        return false;
      }
    }
    return true;
  };

  struct Leaf {
    std::string field;  // path after the base, dots intact
    std::string decl;   // rendered `:type` suffix, or ""
    int64_t     nid{0};
  };
  std::vector<std::string>                            order;
  absl::flat_hash_map<std::string, std::vector<Leaf>> groups;
  absl::flat_hash_set<std::string>                    bare;    // names used WITHOUT a dot
  absl::flat_hash_set<std::string>                    vetoed;  // base cannot be regrouped
  struct Typed_bundle {
    std::vector<Leaf> leaves;
    int64_t           decl_nid{0};
    std::string       mode;
  };
  absl::flat_hash_map<std::string, Typed_bundle> typed_bundles;

  std::function<void(Lnast_nid)> scan = [&](Lnast_nid n) {
    for (auto c = lnast->get_child(n); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (Lnast_ntype::is_ref(lnast->get_type(c))) {
        std::string raw(lnast->get_name(c));
        if (raw.size() >= 2 && raw.front() == '`' && raw.back() == '`') {
          const std::string inner = raw.substr(1, raw.size() - 2);
          const auto        dot   = inner.find('.');
          if (dot != std::string::npos && plain_ident(std::string_view(inner).substr(0, dot))) {
            bool ok = true;
            for (size_t s = dot + 1, e = 0; ok; s = e + 1) {
              e  = inner.find('.', s);
              ok = plain_ident(std::string_view(inner).substr(s, e == std::string::npos ? std::string::npos : e - s));
              if (e == std::string::npos) {
                break;
              }
            }
            if (!ok) {
              vetoed.insert(inner.substr(0, dot));
            }
          } else if (dot != std::string::npos) {
            vetoed.insert(inner.substr(0, dot));
          }
        } else if (raw.find('.') == std::string::npos) {
          bare.insert(raw);
        }
      }
      // Slang's aggregate pseudo-variable is represented as
      //   type_spec(io.operation, u5) ... declare(io, none, wire)
      // rather than one tuple-typed declare. Preserve those field types so the
      // writer can reconstruct `wire io:(operation:u5, ...) = nil` below.
      if (lnast->get_type(c) == Lnast_ntype::Lnast_ntype_type_spec) {
        auto name = lnast->get_child(c);
        auto type = name.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(name);
        if (!name.is_invalid() && !type.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(name))) {
          std::string raw(lnast->get_name(name));
          const auto  dot = raw.find('.');
          if (dot != std::string::npos && raw.front() != '`') {
            typed_bundles[raw.substr(0, dot)].leaves.push_back(
                {raw.substr(dot + 1), render_type_at(type), c.get_class_index().value});
          }
        }
      }
      // A declare of an escaped dotted leaf is the regroup candidate.
      if (Lnast_ntype::is_declare(lnast->get_type(c))) {
        auto v = lnast->get_child(c);
        if (!v.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(v))) {
          std::string raw(lnast->get_name(v));
          if (raw.find('.') == std::string::npos) {
            auto type = lnast->get_sibling_next(v);
            auto qual = type.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(type);
            if (!type.is_invalid() && lnast->get_type(type) == Lnast_ntype::Lnast_ntype_prim_type_none && !qual.is_invalid()
                && (lnast->get_name(qual) == "wire" || lnast->get_name(qual) == "mut")) {
              auto& bundle    = typed_bundles[raw];
              bundle.decl_nid = c.get_class_index().value;
              bundle.mode     = std::string(lnast->get_name(qual));
            }
          }
          if (raw.size() >= 2 && raw.front() == '`' && raw.back() == '`') {
            const std::string inner = raw.substr(1, raw.size() - 2);
            const auto        dot   = inner.find('.');
            if (dot != std::string::npos) {
              const std::string base = inner.substr(0, dot);
              // ONE level only. A multi-level leaf (`` `s.inner.x` ``) would emit
              // its whole dotted remainder as a single tuple FIELD NAME
              // (`mut inner.x = 0`), which is not legal and does not re-parse.
              // Nested regrouping needs a nested literal; until then leave the
              // whole base alone rather than emit something unreadable.
              if (inner.find('.', dot + 1) != std::string::npos) {
                vetoed.insert(base);
                scan(c);
                continue;
              }
              auto              ty   = lnast->get_sibling_next(v);
              auto              qual = ty.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(ty);
              const std::string mode = qual.is_invalid() ? std::string("mut") : std::string(lnast->get_name(qual));
              // Only plain combinational storage: a reg/latch leaf is STATE, and
              // renaming state breaks cross-design name correspondence.
              if (mode != "mut" && mode != "wire") {
                vetoed.insert(base);
              } else if (!find_stages_child(c).is_invalid() || !is_declare_with_value(c)) {
                if (!find_stages_child(c).is_invalid()) {
                  vetoed.insert(base);
                } else {
                  if (groups.find(base) == groups.end()) {
                    order.push_back(base);
                  }
                  groups[base].push_back(
                      {inner.substr(dot + 1), ty.is_invalid() ? std::string{} : render_type_at(ty), c.get_class_index().value});
                }
              } else {
                vetoed.insert(base);  // an inline initializer: not the flat-leaf shape
              }
            }
          }
        }
      }
      scan(c);
    }
  };
  scan(body_nid);

  std::vector<std::string> rebuilt;
  for (auto& [base, bundle] : typed_bundles) {
    if (bundle.decl_nid == 0 || bundle.leaves.empty() || vetoed.count(base) != 0u) {
      continue;
    }
    Port_group_node root;
    bool            bad = false;
    for (const auto& leaf : bundle.leaves) {
      Port_group_node* node  = &root;
      std::string_view path  = leaf.field;
      size_t           start = 0;
      for (;;) {
        const auto  dot  = path.find('.', start);
        const bool  last = dot == std::string_view::npos;
        std::string comp(path.substr(start, last ? std::string_view::npos : dot - start));
        if (!plain_ident(comp)) {
          bad = true;
          break;
        }
        Port_group_node* kid = nullptr;
        for (auto& [name, child] : node->kids) {
          if (name == comp) {
            kid = child.get();
            break;
          }
        }
        if (kid == nullptr) {
          node->kids.emplace_back(comp, std::make_unique<Port_group_node>());
          kid = node->kids.back().second.get();
        } else if (last || kid->is_leaf) {
          bad = true;
          break;
        }
        if (last) {
          kid->is_leaf   = true;
          kid->type_text = leaf.decl;
          break;
        }
        node  = kid;
        start = dot + 1;
      }
      if (bad) {
        break;
      }
    }
    if (bad) {
      continue;
    }
    auto render = [&](auto&& self, const Port_group_node& node) -> std::string {
      std::string out   = "(";
      bool        first = true;
      for (const auto& [name, child] : node.kids) {
        if (!first) {
          out += ", ";
        }
        out += quote_kw_path(name);
        if (child->is_leaf) {
          if (!child->type_text.empty()) {
            out += ":" + child->type_text;
          }
        } else {
          out += ":" + self(self, *child);
        }
        first = false;
      }
      return out + ")";
    };
    std::string text = bundle.mode + " " + quote_kw_path(base) + ":" + render(render, root);
    if (bundle.mode == "wire") {
      text += " = nil";
    }
    body_bundle_text_[bundle.decl_nid] = std::move(text);
    declared_.insert(base);
    for (const auto& leaf : bundle.leaves) {
      bundle_fields_[base].insert(leaf.field);
      declared_.insert(base + "." + leaf.field);
    }
  }
  for (const auto& base : order) {
    const auto& leaves = groups[base];
    if (leaves.size() < 2 || vetoed.count(base) != 0u || bare.count(base) != 0u || declared_.count(base) != 0u
        || bundle_fields_.count(base) != 0u) {
      continue;  // ambiguous, already a real name, or already a port bundle
    }
    absl::flat_hash_set<std::string> seen;
    bool                             dup = false;
    for (const auto& l : leaves) {
      dup = dup || !seen.insert(l.field).second;
    }
    if (dup) {
      continue;
    }
    std::string text  = "mut " + base + " = (";
    bool        first = true;
    for (const auto& l : leaves) {
      if (!first) {
        text += ", ";
      }
      first  = false;
      text  += "mut " + quote_kw_path(l.field);
      if (!l.decl.empty()) {
        text += ":" + l.decl;
      }
      text += " = 0";
    }
    text += ")";
    body_bundle_text_.emplace(leaves.front().nid, std::move(text));
    for (size_t i = 1; i < leaves.size(); ++i) {
      body_bundle_skip_.insert(leaves[i].nid);
    }
    rebuilt.push_back(base);
    declared_.insert(base);
    for (const auto& l : leaves) {
      bundle_fields_[base].insert(l.field);
      if (auto q = quote_kw_path(l.field); q != l.field) {
        bundle_fields_[base].insert(q);
      }
      declared_.insert(base + "." + l.field);
    }
  }
  std::vector<std::string> zero_fields;
  for (const auto& base : rebuilt) {
    for (const auto& l : groups[base]) {
      zero_fields.push_back(base + "." + l.field);
    }
  }
  drop_redundant_bundle_zeros(body_nid, zero_fields);
}

// The rebuilt literal already initializes every field to 0:
//
//   mut sigs_qual_exa_h = (mut cmd = 0, mut add = 0, …)
//   sigs_qual_exa_h.add = 0     <- the front end's flat per-leaf seed: 0 over 0
//   sigs_qual_exa_h.cmd = 0
//
// so those seeds are pure duplication (6186 lines in one minion re-emit). Drop
// each while its field is still KNOWN zero — a `= 0` that lands AFTER any other
// write to the field is a real reset and stays. A write anywhere inside a
// nested scope counts, since it may or may not have run.
void Lnast_prp_writer::drop_redundant_bundle_zeros(Lnast_nid body_nid, const std::vector<std::string>& zero_fields) {
  if (zero_fields.empty()) {
    return;
  }
  // "base.field" still holding the literal's 0
  absl::flat_hash_set<std::string> zero(zero_fields.begin(), zero_fields.end());
  auto                             unquote = [](std::string_view s) {
    return (s.size() >= 2 && s.front() == '`' && s.back() == '`') ? std::string(s.substr(1, s.size() - 2)) : std::string(s);
  };
  // Any def of `base.field` — or of the whole `base` — ends its known-zero run.
  std::function<void(Lnast_nid)> kill = [&](Lnast_nid n) {
    auto v = lnast->get_child(n);
    // A DECLARE is not a write: the per-leaf declares are precisely what the
    // literal absorbed, so counting them here would clear every field before
    // the first store is even reached.
    if (!v.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(v)) && defines_child0(lnast->get_type(n))
        && !Lnast_ntype::is_declare(lnast->get_type(n))) {
      const std::string nm = unquote(strip_prefix(lnast->get_name(v)));
      zero.erase(nm);
      if (nm.find('.') == std::string::npos) {
        const std::string pfx = nm + ".";
        absl::erase_if(zero, [&](const std::string& k) { return k.compare(0, pfx.size(), pfx) == 0; });
      }
    }
    for (auto c = lnast->get_child(n); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      kill(c);
    }
  };
  // Statement walk. An `if` is followed INTO each arm — a decoder writes whole
  // bundles arm by arm (`unique if … { sigs.add = 0, sigs.cmd = 20, … }`), and an
  // arm's `= 0` is just as redundant as a top-level one when the field is still
  // zero where the arm starts. After the if, a field counts as zero only if
  // EVERY arm left it zero: an arm may not run, but any arm may.
  std::function<void(Lnast_nid)> process = [&](Lnast_nid stmts_nid) {
    for (auto c = lnast->get_child(stmts_nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      const auto t = lnast->get_type(c);
      if (t == Lnast_ntype::Lnast_ntype_store) {
        auto v = lnast->get_child(c);
        if (!v.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(v))) {
          auto val = lnast->get_sibling_next(v);
          if (!val.is_invalid() && lnast->is_last_child(val) && lnast->get_type(val) == Lnast_ntype::Lnast_ntype_const
              && lnast->get_name(val) == "0" && zero.count(unquote(strip_prefix(lnast->get_name(v)))) != 0) {
            dead_init_stmts_.insert(c.get_class_index().value);
            continue;  // the field is still zero: nothing was written
          }
        }
        kill(c);
        continue;
      }
      if (t == Lnast_ntype::Lnast_ntype_if || t == Lnast_ntype::Lnast_ntype_unique_if) {
        for (auto a = lnast->get_child(c); !a.is_invalid(); a = lnast->get_sibling_next(a)) {
          if (!Lnast_ntype::is_stmts(lnast->get_type(a))) {
            kill(a);  // a condition can define too; it runs before every arm
          }
        }
        const absl::flat_hash_set<std::string> entry = zero;
        absl::flat_hash_set<std::string>       merged;
        bool                                   first_arm = true;
        for (auto a = lnast->get_child(c); !a.is_invalid(); a = lnast->get_sibling_next(a)) {
          if (!Lnast_ntype::is_stmts(lnast->get_type(a))) {
            continue;
          }
          zero = entry;
          process(a);
          if (first_arm) {
            merged    = zero;
            first_arm = false;
          } else {
            absl::erase_if(merged, [&](const std::string& k) { return zero.count(k) == 0; });
          }
        }
        zero = first_arm ? entry : merged;
        continue;
      }
      kill(c);  // anything else (a loop body included): every def it holds counts
    }
  };
  process(body_nid);
}

// Emit `(in0:T0, in1:T1, …) -> (out0:T0, …)` from the io node.  The io node has
// two `tuple_add` children: the first groups input ports, the second outputs.
// Each port is `store(ref(name), const(init|nil), type, [stages])`.
void Lnast_prp_writer::emit_port_group(Lnast_nid tup_nid, bool is_output, bool is_mod) {
  print("(");
  bool first = true;
  if (!tup_nid.is_invalid()) {
    for (auto port = lnast->get_child(tup_nid); !port.is_invalid(); port = lnast->get_sibling_next(port)) {
      // A re-nested tuple-port leaf (collect_port_groups): the base's FIRST
      // leaf prints the whole reconstructed `base:(field:T, …)` entry (stages
      // included) in its position; the later leaves print nothing.
      if (port_group_skip_.count(port.get_class_index().value) != 0u) {
        continue;
      }
      if (auto git = port_group_text_.find(port.get_class_index().value); git != port_group_text_.end()) {
        if (!first) {
          print(", ");
        }
        print(git->second);
        first = false;
        continue;
      }
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
      // A port whose SV dim named a package param prints the imported alias
      // (`cmd:vpu_defs_pkg.VPU_FCMD_SZ_T`) instead of the concretized `u7`.
      bool emitted_type = false;
      if (auto ait = lnast->get_io_type_names().find(std::string(pname)); ait != lnast->get_io_type_names().end()) {
        print(":");
        print(ait->second);
        emitted_type = true;
        typed_emitted_.insert(std::string(pname));  // the signature states this port's width
        // The alias is what PRINTS, but the concretized `uN` is still what the
        // port holds — record it so a whole-width mask on this port still folds.
        if (!type_nid.is_invalid()) {
          note_port_width(pname, render_type_at(type_nid));
        }
      } else if (!type_nid.is_invalid()) {
        auto t = render_type_at(type_nid);
        if (!t.empty()) {
          print(":");
          print(t);
          emitted_type = true;
          note_port_width(pname, t);
          typed_emitted_.insert(std::string(pname));
        }
      }
      if (!emitted_type) {
        // A bool port may carry its type only in io_meta after upass. If the
        // writer drops it, the emitted lambda becomes an untyped template and
        // tolg legitimately produces no graph on recompile. Spell it `u1`, the
        // writer's historical boundary representation for a one-bit port.
        const auto& entries = is_output ? lnast->io_meta().outputs : lnast->io_meta().inputs;
        for (const auto& entry : entries) {
          if (entry.name == lnast->get_name(name_nid) && entry.kind == Io_kind::boolean) {
            print(":u1");
            break;
          }
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
  // LAST unquoted dot: an already-escaped `` `a.b` `` name owns its dot, so
  // rfind('.') would cut the quoted span and leave a dangling backtick.
  size_t           dot  = std::string_view::npos;
  for (size_t at = 0; (at = next_unquoted_dot(full, at)) != std::string_view::npos; ++at) {
    dot = at;
  }
  std::string_view tail = (dot == std::string_view::npos) ? full : full.substr(dot + 1);
  if (tail.empty()) {
    tail = full;
  }
  return quote_module_path(tail);  // a Verilog escaped id (`\s\m`) needs backticks
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
    if (val_is_ref) {
      // This net is read from a DECLARE-folded attribute, and declares are emitted
      // ahead of every body write (the pass-0/pass-1 split) — so the read lands
      // before the net's own driver.  Only a `_pin` key gets its driver relocated
      // ahead of the declare (pin_dep_nets_); for every OTHER key the read is
      // simply early, so the net must keep a hoisted `mut <net> = 0` declaration
      // rather than be declared in place by its store.
      folded_attr_refs_.insert(val);
      const auto owner = std::string(strip_prefix(lnast->get_name(var_nid)));
      folded_attr_refs_by_owner_[owner].insert(val);
      folded_attr_owners_by_ref_[val].insert(owner);
      if (key == "clock_pin" || key == "reset_pin" || key.ends_with("_pin")) {
        pin_dep_nets_.insert(val);
        val = "ref " + val;
      }
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

const std::string& Lnast_prp_writer::cached_strip_prefix(int32_t name_id) const {
  auto [it, inserted] = stripped_name_cache_.try_emplace(name_id);
  if (inserted) {
    it->second = strip_prefix(lnast->resolve_name(name_id));
  }
  return it->second;
}

void Lnast_prp_writer::collect_node_read_ids(Lnast_nid node, absl::flat_hash_set<int32_t>& out) const {
  // A ref that is an inlined single-use temp is replaced by the operands of
  // ITS definition (so `gclk = clk_b & inv` whose `&` rides a folded temp still
  // reports the real read `inv`).  Folds are acyclic (def precedes use), so this
  // terminates.
  if (Lnast_ntype::is_ref(lnast->get_type(node))) {
    const auto name_id = lnast->get_name_id(node);
    auto       fit     = fold_info_id_.find(name_id);
    if (foldable_id_.contains(name_id) && fit != fold_info_id_.end()) {
      auto d0 = lnast->get_child(fit->second->def_node);
      for (auto c = d0.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(d0); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
        collect_node_read_ids(c, out);
      }
    } else {
      out.insert(name_id);
    }
    return;
  }
  for (auto c = lnast->get_child(node); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    collect_node_read_ids(c, out);
  }
}

void Lnast_prp_writer::collect_node_reads(Lnast_nid node, absl::flat_hash_set<std::string>& out) const {
  const auto key = node.get_class_index().value;
  auto       it  = node_read_ids_cache_.find(key);
  if (it == node_read_ids_cache_.end()) {
    absl::flat_hash_set<int32_t> ids;
    collect_node_read_ids(node, ids);
    it = node_read_ids_cache_.emplace(key, std::vector<int32_t>(ids.begin(), ids.end())).first;
  }
  for (const auto name_id : it->second) {
    out.insert(cached_strip_prefix(name_id));
  }
}

void Lnast_prp_writer::collect_driver_reads(Lnast_nid def_node, absl::flat_hash_set<std::string>& out) const {
  auto c0 = lnast->get_child(def_node);
  if (c0.is_invalid()) {
    return;
  }
  for (auto c = lnast->get_sibling_next(c0); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    collect_node_reads(c, out);
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
      // Some statements decide to emit NOTHING only once they are being written
      // (write_store's self-store folds, which must render the RHS first). Rewind
      // the indent instead of leaving a whitespace-only line behind.
      const auto before = os.tellp();
      print_indent();
      const auto after_indent = os.tellp();
      write_node();
      if (before != std::streampos(-1) && os.tellp() == after_indent) {
        os.seekp(before);
        continue;
      }
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

bool Lnast_prp_writer::try_write_match() {
  // children: [cond, stmts, cond, stmts, …, (else_stmts)]
  std::vector<Lnast_nid> kids;
  for (auto c = lnast->get_child(cur); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    kids.push_back(c);
  }
  const bool   has_else = (kids.size() % 2) == 1;
  const size_t npairs   = kids.size() / 2;
  if (npairs == 0) {
    return false;
  }
  // A match label must be a CONSTANT (an SV case item): a literal const or an
  // imported pkg.PARAM comptime ref.
  auto label_ok = [&](Lnast_nid n) {
    auto t = lnast->get_type(n);
    if (t == Lnast_ntype::Lnast_ntype_const) {
      return true;
    }
    if (Lnast_ntype::is_ref(t)) {
      // a pkg.PARAM ref arrives as a tuple_get temp — check its rendered form
      return is_imported_pkg_path(render_value(n, /*operand_ctx=*/true));
    }
    return false;
  };
  // Collect each condition's (a, b) eq operand pairs (through `or` chains).
  std::function<bool(Lnast_nid, std::vector<std::pair<Lnast_nid, Lnast_nid>>&)> collect_eqs
      = [&](Lnast_nid def, std::vector<std::pair<Lnast_nid, Lnast_nid>>& out) -> bool {
    const auto t = lnast->get_type(def);
    if (t == Lnast_ntype::Lnast_ntype_eq) {
      auto c0 = lnast->get_child(def);
      if (c0.is_invalid()) {
        return false;
      }
      auto a = lnast->get_sibling_next(c0);
      if (a.is_invalid()) {
        return false;
      }
      auto b = lnast->get_sibling_next(a);
      if (b.is_invalid() || !lnast->is_last_child(b)) {
        return false;
      }
      out.emplace_back(a, b);
      return true;
    }
    if (t == Lnast_ntype::Lnast_ntype_log_or) {
      auto c0 = lnast->get_child(def);
      if (c0.is_invalid()) {
        return false;
      }
      for (auto o = lnast->get_sibling_next(c0); !o.is_invalid(); o = lnast->get_sibling_next(o)) {
        if (!Lnast_ntype::is_ref(lnast->get_type(o))) {
          return false;
        }
        std::string on(lnast->get_name(o));
        if (!is_foldable(on)) {
          return false;
        }
        if (!collect_eqs(fold_info_.at(on).def_node, out)) {
          return false;
        }
      }
      return true;
    }
    return false;
  };
  std::vector<std::vector<std::pair<Lnast_nid, Lnast_nid>>> arm_eqs(npairs);
  for (size_t p = 0; p < npairs; ++p) {
    auto cnd = kids[2 * p];
    if (!Lnast_ntype::is_ref(lnast->get_type(cnd))) {
      return false;
    }
    std::string cn(lnast->get_name(cnd));
    if (!is_foldable(cn)) {
      return false;
    }
    if (!collect_eqs(fold_info_.at(cn).def_node, arm_eqs[p]) || arm_eqs[p].empty()) {
      return false;
    }
  }
  // Same scrutinee on one side of EVERY eq; the other side is the label.
  auto try_side = [&](bool scrut_is_a, std::string& scr, std::vector<std::vector<std::string>>& labels) {
    scr = render_value(scrut_is_a ? arm_eqs[0][0].first : arm_eqs[0][0].second, /*operand_ctx=*/true);
    labels.assign(npairs, {});
    for (size_t p = 0; p < npairs; ++p) {
      for (const auto& [a, b] : arm_eqs[p]) {
        auto      sa = render_value(a, /*operand_ctx=*/true);
        auto      sb = render_value(b, /*operand_ctx=*/true);
        Lnast_nid label_nid;
        if (sa == scr) {
          label_nid = b;
        } else if (sb == scr) {
          label_nid = a;
        } else {
          return false;
        }
        if (!label_ok(label_nid)) {
          return false;
        }
        labels[p].push_back(label_nid.get_class_index().value == a.get_class_index().value ? sa : sb);
      }
    }
    return true;
  };
  std::string                           scr;
  std::vector<std::vector<std::string>> labels;
  if (!try_side(true, scr, labels) && !try_side(false, scr, labels)) {
    return false;
  }
  // ── print ──────────────────────────────────────────────────────────────────
  print("match ");
  print(scr);
  print(" {\n");
  ++depth;
  if (!move_to_child()) {
    --depth;
    print("}");
    return true;
  }
  size_t idx = 0;
  do {
    if (idx % 2 == 0 && idx / 2 < npairs) {
      ++idx;
      continue;  // condition child — encoded in the arm label
    }
    const size_t p = (idx - 1) / 2;
    print_indent();
    if (has_else && idx == kids.size() - 1) {
      print("else {\n");
    } else if (labels[p].size() == 1) {
      print("== " + labels[p][0] + " {\n");
    } else {
      std::string s = "in (";
      for (size_t k = 0; k < labels[p].size(); ++k) {
        s += (k != 0u ? ", " : "") + labels[p][k];
      }
      print(s + ") {\n");
    }
    ++depth;
    write_node();
    --depth;
    print_indent();
    print("}\n");
    ++idx;
  } while (move_to_sibling());
  --depth;
  print_indent();
  print("}");
  move_to_parent();
  return true;
}

Lnast_nid Lnast_prp_writer::flattenable_nested_if(Lnast_nid stmts_nid) const {
  Lnast_nid found{};
  int       real = 0;
  for (auto c = lnast->get_child(stmts_nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (is_folded_node(c) || emits_nothing_stmt(c)) {
      continue;  // an inlined temp def (e.g. the nested if's condition) or a no-op
    }
    if (++real > 1 || lnast->get_type(c) != Lnast_ntype::Lnast_ntype_if) {
      return Lnast_nid{};
    }
    if (mux_info_.count(c.get_class_index().value) != 0u) {
      return Lnast_nid{};  // renders as an assignment — keep the else block
    }
    found = c;
  }
  return real == 1 ? found : Lnast_nid{};
}

void Lnast_prp_writer::write_if() { write_if_chain(/*continuation=*/false); }

void Lnast_prp_writer::write_if_chain(bool continuation) {
  // Mux collapse: render `x = if c0 {v0} elif c1 {v1} … else {D}` instead of the
  // statement-if. The cursor stays on the if-node (no navigation) so the caller's
  // move_to_sibling still advances correctly. x is already declared (its poison
  // declare / hoist precedes this if); the default store was suppressed.
  if (auto mit = mux_info_.find(cur.get_class_index().value); mit != mux_info_.end()) {
    const Mux_info& mi = mit->second;
    std::string     lhs(strip_prefix(mi.lhs));
    std::string     s;
    // Declare here (`mut x = if …`) ONLY when nothing has declared `x` yet.
    // `fold_decl` means the poison DECLARE node was folded into this mux — but a
    // poison STORE (`x = 0ub?`) that was NOT adjacent to the mux (e.g. an SSA
    // `x__w1` sits between them, as the provenance flow produces) still emits
    // `mut x = 0ub?` ahead of us and declares `x`; a second `mut` here is a
    // redeclaration. Keying on declared_ alone covers both: folded-and-dropped
    // poison (x not declared → `mut`), and a surviving poison store (x declared
    // → plain reassign).
    if (!declared_.count(lhs)) {
      s += "mut ";
      declared_.insert(lhs);
      s += lhs;
      if (!mi.decl_type.empty()) {
        s += ":" + mi.decl_type;
      }
      s += " = ";
    } else {
      s += lhs + " = ";
    }
    s += render_mux_expr(mi);
    print(s);
    return;
  }

  const bool unique = Lnast_ntype::is_unique_if(current_ntype());
  if (!continuation && unique && try_write_match()) {
    return;
  }
  // A CONDITION-LESS if: children are [cond, stmts]* [else-stmts], so a lone
  // `stmts` child means every condition arm is gone and only the else/`default`
  // survives.  (An SV `case (x) default: … endcase` reads in exactly this shape.)
  // The generic path below renders child0 as the CONDITION, so it emitted
  // `unique if  {\n}` — no condition AND no body, which does not re-parse, so the
  // v -> prp -> v round trip died at re-read.  With no conditions the else body
  // runs unconditionally: splice its statements in at THIS level.  Not a braced
  // block and not `if true {…}` — both open a scope, which would hide any
  // declaration the arm makes from the statements that follow it.
  {
    int       nkids = 0;
    Lnast_nid only{};
    for (auto c = lnast->get_child(cur); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      only = c;
      ++nkids;
    }
    if (nkids == 1 && Lnast_ntype::is_stmts(lnast->get_type(only))) {
      move_to_child();  // -> the else-stmts
      bool first_out = true;
      if (move_to_child()) {
        do {
          if (is_folded_node(cur) || emits_nothing_stmt(cur)) {
            continue;
          }
          const auto before = os.tellp();
          if (!first_out) {
            os << "\n";
            print_indent();  // the caller indented the first line for us
          }
          const auto after_sep = os.tellp();
          write_node();
          if (before != std::streampos(-1) && os.tellp() == after_sep) {
            os.seekp(before);  // statement emitted nothing — drop its separator too
            continue;
          }
          first_out = false;
        } while (move_to_sibling());
        move_to_parent();
      }
      move_to_parent();
      return;
    }
  }
  if (!move_to_child()) {
    return;
  }

  // First child: condition (ref or const) — inline a single-use temp condition.
  print(continuation ? " elif " : (unique ? "unique if " : "if "));
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
      // An else block holding ONLY a plain nested if (the reader's shape for an
      // SV `else if` ladder) flattens to ` elif … ` — a 10-deep nest becomes a
      // flat chain. A `unique if` chain never absorbs a plain if (semantics).
      if (auto nested = unique ? Lnast_nid{} : flattenable_nested_if(cur); !nested.is_invalid()) {
        move_to_child();
        while (cur.get_class_index().value != nested.get_class_index().value && move_to_sibling()) {
        }
        write_if_chain(/*continuation=*/true);
        move_to_parent();  // back to the else-stmts; it is the last child, so the loop ends
        continue;
      }
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
  // A re-nested struct local (collect_body_bundles): the base's FIRST leaf
  // declare prints the whole rebuilt `mut base = (mut f = 0, …)`, the rest print
  // nothing (their field already exists in the literal).
  if (const auto key = cur.get_class_index().value; body_bundle_skip_.count(key) != 0u) {
    return;
  } else if (auto bit = body_bundle_text_.find(key); bit != body_bundle_text_.end()) {
    print(bit->second);
    return;
  }
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
  // A value-less declare of a COMPILER TEMP whose value arrives as a later store:
  // emitting it standalone forces a seed (`mut t = 0`), and the store then either
  // rebinds a `const` or changes the kind ("cannot assign boolean value to
  // `t2920` (it is integer)"). uPass emits that pair adjacent and in one scope, so
  // drop the declaration and let decl_prefix mint it on the store instead. Note
  // this must run BEFORE the `declared_` insert, or decl_prefix would see the
  // name as already declared and emit a bare re-assignment.
  // An inline `= 0` seed is the same trap: uPass emits `declare(%t, …, mut,
  // const 0)` right before the op that defines %t, so the seed pins the temp to
  // INTEGER and a boolean-valued definition then fails the kind check. Only a
  // plain-const seed is dropped (a tuple/array initializer is real content), and
  // only for an UNTYPED declare — a typed one pins a width the def would lose.
  declared_.insert(std::string(lhs));  // an explicit declare; later writes skip the `mut`

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

  // A file-scope `pub type T = u7` reaches here as `declare(T, prim_type_int,
  // 'type')` with no value: the alias IS the type, so `type T:u7 = 0` (what the
  // generic path printed) is both wrong and unparseable.
  if (kw == "type" && !type_suffix.empty()) {
    move_to_parent();
    if (is_pub_export(lhs)) {
      print("pub ");
    }
    print("type ");
    print(lhs);
    print(" = ");
    print(type_suffix);
    return;
  }
  // A file-scope `pub const K = <expr>` is folded by the time the writer runs —
  // the declare keeps no value and constprop parked the comptime result in
  // pub_values. Print THAT, not the generic path's `= 0`: this file is a
  // package, and a zeroed export silently changes every importer's arithmetic.
  if (file_level_ && kw.starts_with("const") && is_pub_export(lhs)) {
    for (const auto& [path, text] : lnast->get_pub_values()) {
      if (path != lhs) {
        continue;
      }
      move_to_parent();
      // The qualifier arrives as `const` or `const comptime`; Pyrope spells the
      // modifier FIRST (`pub comptime const NAME`).
      print(kw.find("comptime") == std::string::npos ? "pub const " : "pub comptime const ");
      print(lhs);
      if (!type_suffix.empty()) {
        print(":");
        print(type_suffix);
      }
      print(" = ");
      print(text);
      return;
    }
  }

  const bool has_value = move_to_sibling();  // optional inline init

  // A REDUNDANT declaration: `mut`/`const` only (never a `reg`/`wire`/`latch`,
  // whose declaration IS the storage element), untyped (the def carries no width
  // pin), value-less or a plain const seed, and superseded by a real def that
  // PRECEDES every read. Emitting it forces a `mut t194 = 0` line that the next
  // statement overwrites — the noise this removes is ~6% of a generated file —
  // and it also pre-declares the name so its def cannot mint the keyword.
  // Verilog-generated Pyrope carries real source names (`t194`) in this shape,
  // so this is deliberately NOT limited to compiler temps.
  if ((kw == "mut" || kw.starts_with("const")) && type_suffix.empty() && !is_pub_export(lhs)
      && (!has_value || current_ntype() == Lnast_ntype::Lnast_ntype_const) && nested_def_names_.count(std::string(lhs)) == 0) {
    const std::string nm(lhs);
    auto              di = def_idx_.find(nm);
    auto              ri = read_idx_.find(nm);
    if (di != def_idx_.end() && (ri == read_idx_.end() || di->second < ri->second)) {
      declared_.erase(nm);  // the def mints the keyword instead
      move_to_parent();
      return;
    }
  }

  // A value-less `const` declare whose value arrives as a LATER store (uPass
  // splits `const x = e` into declare + store) cannot keep the `const`: the
  // generic path seeds it `= 0` and the store is then a rebind ("const `x`
  // rebind (assigned 2 times)"). Demote to `mut` — a single-assignment net is
  // the same hardware either way, and the declared type survives.
  if (!has_value && kw.starts_with("const")) {
    if (auto dc = def_count_.find(std::string(lhs)); dc != def_count_.end() && dc->second > 0) {
      kw = "mut";
    }
  }

  // A `const 'nil'` value is the slang reader's "no reset / no initializer"
  // sentinel — emit a bare declaration (`reg r:u8`) so tolg gives the flop no
  // reset pin (sync reset is carried by the body mux instead).  `= nil` is not
  // a reparsable initializer for an integer reg.
  const bool nil_value = has_value && current_ntype() == Lnast_ntype::Lnast_ntype_const && current_text() == "nil";

  // The Pyrope grammar has no `latch` declaration keyword: spell a latch as
  // `reg x:T:[latch=true]` — prp2lnast converts it back to a mode-"latch"
  // declare (the shape the slang reader emits and tolg lowers to Ntype Latch).
  std::string extra_attr;
  if (kw == "latch") {
    kw         = "reg";
    extra_attr = "latch=true";
  }

  // See const_nil_wire_ (write_module): a `wire` whose single write precedes
  // every read is spelled as the equivalent `const X:T = nil` forward
  // declaration.
  const bool wire_as_const = kw == "wire" && const_nil_wire_.count(std::string(lhs)) != 0u;
  if (wire_as_const) {
    kw = "const";
  }

  print(kw);
  print(" ");
  print(lhs);
  if (!type_suffix.empty()) {
    typed_emitted_.insert(std::string(lhs));  // the source states this name's width
    print(":");
    print(type_suffix);
  }
  // Fold any collected reg/memory attributes onto the declaration.  With a type
  // the suffix is `:[…]` (single colon after the type); without one it is the
  // `::[…]` prefix form.
  auto it = folded_attrs_.find(std::string(lhs));
  if (it != folded_attrs_.end() || !extra_attr.empty()) {
    print(type_suffix.empty() ? "::[" : ":[");
    if (it != folded_attrs_.end()) {
      print(it->second);
      if (!extra_attr.empty()) {
        print(", ");
      }
    }
    print(extra_attr);
    print("]");
  }
  if (wire_as_const) {
    print(" = nil");  // a forward declaration: the body store is its one bind
  } else if (has_value && !nil_value) {
    print(" = ");
    if (current_ntype() == Lnast_ntype::Lnast_ntype_tuple_add) {
      write_tuple_literal();  // memory init: a bare tuple_add (no LHS child)
    } else if (auto sh = x_poison_shorthand(cur, lhs); !sh.empty()) {
      print(sh);  // the declared width is right here: `mut x:u48 = 0sb?`
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

// True if `n` is a declare initializer made only of compile-time constants: a
// `const`, or a `tuple_add` every child of which is one.  Only such an
// initializer may be MOVED (the nested-mut hoist relocates it from inside an
// `if` to the function top): a `ref`/op/func_call operand reads a net defined
// LATER in the body, and hoisting it above that definition emits Pyrope that
// fails the recompile's "read of undefined variable" check.
// `const 'nil'` is excluded — it is the slang reader's "no reset / no
// initializer" sentinel, and `= nil` is not a reparsable integer initializer
// (write_declare suppresses it for the same reason).
bool Lnast_prp_writer::is_comptime_init(Lnast_nid n) const {
  if (n.is_invalid()) {
    return false;
  }
  const auto t = lnast->get_type(n);
  if (Lnast_ntype::is_const(t)) {
    return lnast->get_name(n) != "nil";
  }
  if (t != Lnast_ntype::Lnast_ntype_tuple_add) {
    return false;
  }
  auto c = lnast->get_child(n);
  if (c.is_invalid()) {
    return false;  // an empty tuple carries no value
  }
  for (; !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (!is_comptime_init(c)) {
      return false;
    }
  }
  return true;
}

// Render an `is_comptime_init` node as Pyrope text: `5`, or `(1, 2)` for a
// tuple_add.  Same output as write_tuple_literal(), but nid-based and returning
// a string — the hoist runs before the cursor walk, so it cannot drive `cur`.
std::string Lnast_prp_writer::render_comptime_init(Lnast_nid n) {
  if (lnast->get_type(n) != Lnast_ntype::Lnast_ntype_tuple_add) {
    return render_value(n, /*operand_ctx=*/false);
  }
  std::string out("(");
  bool        first = true;
  for (auto c = lnast->get_child(n); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (!first) {
      out += ", ";
    }
    out   += render_comptime_init(c);
    first  = false;
  }
  out += ")";
  return out;
}

// True when [lo..=hi] covers every bit `src` can hold, so the mask selects the
// value unchanged and is a no-op the emitted source is better without. Decided
// from `known_unsigned_bits`: a `uN` port or declared variable is the common
// case (`a_i#[0..=51]` on `a_i:u52`), a temp whose definition bounds it (a
// narrower slice, a sized concat) the rest; anything unproven stays as written.
// Only an UNSIGNED bound counts: on a signed value the same mask REINTERPRETS
// it as unsigned, which is a real operation.
bool Lnast_prp_writer::is_whole_width_mask(Lnast_nid src, int lo, int hi) const {
  if (lo != 0 || hi < 0) {
    return false;
  }
  return fits_unsigned_bits(src, static_cast<int64_t>(hi) + 1);
}

// A numeric const leaf's CANONICAL Pyrope spelling. Dlop::to_pyrope is the
// round-tripping printer — small values decimal, wide ones hex, unknown
// patterns `0ub…`/`0sb…` — so the emitted source stops carrying whatever
// spelling the pass that minted the literal happened to use. The visible win is
// the 64-bit masks an x-pattern compare produces: `& 18446744073709551614`
// prints as `& 0xfffffffffffffffe`. Text that does not parse (or is not a
// number) is passed through untouched.
std::string Lnast_prp_writer::canonical_const_text(std::string_view txt) {
  if (txt.empty() || (std::isdigit(static_cast<unsigned char>(txt.front())) == 0 && txt.front() != '-')) {
    return std::string(txt);
  }
  try {
    const Dlop& d = Dlop::from_pyrope_cached(txt);
    // An all-`?` pattern drops to_pyrope's leading sign digit (`0ub0???` ->
    // `0ub???`): an unsigned literal zero-extends, so the two are the same
    // value (checked by a cassert), and the shorter one is what the width
    // reads as.
    if (d.is_integer() && d.has_unknowns() && txt.size() > 4 && txt.starts_with("0ub0")
        && txt.find_first_not_of('?', 4) == std::string_view::npos) {
      return std::string("0ub").append(txt.substr(4));
    }
    // Otherwise a boolean / string / nil / x-PATTERN keeps its own spelling:
    // for a pattern the written form already IS the canonical binary one.
    if (d.is_integer() && !d.has_unknowns()) {
      auto s = d.to_pyrope();
      if (!s.empty()) {
        // to_pyrope's multi-word hex path prints the top word with `{:x}`, so a
        // positive value that needed a zero headroom word for its sign leads
        // with it: `0x0ffffffff0000707f`. Leading zeros never change a hex
        // literal's value (checked: `0x0ff == 0xff`), so drop them.
        const size_t pfx = s.starts_with("-0x") ? 3 : (s.starts_with("0x") ? 2 : 0);
        if (pfx != 0) {
          size_t nz = s.find_first_not_of('0', pfx);
          if (nz == std::string::npos) {
            nz = s.size() - 1;  // all zeros: keep one digit
          }
          s.erase(pfx, nz - pfx);
        }
        return s;
      }
    }
  } catch (...) {  // NOLINT(bugprone-empty-catch) — an unparseable literal is emitted as written
  }
  return std::string(txt);
}

// Parse a const leaf's text as a NON-NEGATIVE integer with no unknown bits: the
// only literal whose unsigned window is its own magnitude. Null for anything
// else (`nil`, a string, a negative, an x-pattern). The parse is the shared
// memo, so re-asking about the same literal is a hash lookup.
static const Dlop* plain_uint_literal(std::string_view txt) {
  if (txt.empty()) {
    return nullptr;
  }
  try {
    const Dlop& d = Dlop::from_pyrope_cached(txt);
    if (d.is_integer() && !d.has_unknowns() && !d.is_negative()) {
      return &d;
    }
  } catch (...) {  // NOLINT(bugprone-empty-catch) — an unparseable literal simply has no window
  }
  return nullptr;
}

std::optional<int> Lnast_prp_writer::known_unsigned_bits(Lnast_nid n, int walk_depth) const {
  using N = Lnast_ntype;
  if (n.is_invalid() || walk_depth > 8) {  // a re-converging DAG is walked once per edge: bound it
    return std::nullopt;
  }
  const auto t = lnast->get_type(n);
  if (N::is_const(t)) {
    const auto* d = plain_uint_literal(lnast->get_name(n));
    if (d == nullptr) {
      return std::nullopt;
    }
    return d->get_bits() > 0 ? d->get_bits() - 1 : 0;  // get_bits() counts the sign slot
  }
  if (N::is_ref(t)) {
    const std::string nm(lnast->get_name(n));
    if (auto pit = port_bits_.find(std::string(strip_prefix(nm))); pit != port_bits_.end()) {
      return pit->second;
    }
    auto fit = fold_info_.find(nm);
    if (fit == fold_info_.end() || fit->second.def_count - fit->second.decl_defs != 1) {
      return std::nullopt;  // no def, or several: this read's value is not pinned to one expression
    }
    // A DECLARE alongside the value def can carry an initializer, and a read
    // that lands on that initializer instead (a def inside one if-arm, read
    // outside it) is a different, possibly wider value. Only a name whose
    // single def is its ONLY def — or a compiler temp, which the front end
    // mints fresh per operation — is pinned. A declared `uN` variable is
    // covered above, by its type.
    if (fit->second.decl_defs != 0 && !is_tmp(nm)) {
      return std::nullopt;
    }
    return known_unsigned_bits(fit->second.def_node, walk_depth + 1);
  }

  auto c0 = lnast->get_child(n);
  switch (t) {
    case N::Lnast_ntype_get_mask: {
      // The emitted `s#[lo..=hi]` is an unsigned select of hi-lo+1 bits. A
      // ONE-bit select is excluded: its constant fold is the signed -1/0
      // boolean (Dlop::get_mask_op), so it carries no unsigned window.
      auto src  = c0.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(c0);
      auto mask = src.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(src);
      if (mask.is_invalid()) {
        return std::nullopt;
      }
      if (lnast->get_type(mask) == N::Lnast_ntype_ref) {
        auto rit = range_lohi_.find(std::string(lnast->get_name(mask)));
        if (rit == range_lohi_.end()) {
          return std::nullopt;
        }
        const auto lo = parse_int_const(rit->second.first);
        const auto hi = parse_int_const(rit->second.second);
        if (!lo || !hi || *lo < 0 || *hi < *lo + 1) {
          return std::nullopt;
        }
        return static_cast<int>(*hi - *lo + 1);
      }
      if (lnast->get_type(mask) != N::Lnast_ntype_const) {
        return std::nullopt;
      }
      auto run = contiguous_run(lnast->get_name(mask));  // the compacting (`#[lo..=hi]`) spelling
      if (!run || run->second - run->first < 1) {
        return std::nullopt;
      }
      return run->second - run->first + 1;
    }
    case N::Lnast_ntype_concat: {
      // sum(lane widths) — the concat's result is that non-negative width.
      int64_t total = 0;
      for (auto v = c0.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(c0); !v.is_invalid();) {
        auto w = lnast->get_sibling_next(v);
        if (w.is_invalid()) {
          return std::nullopt;
        }
        const auto* d = plain_uint_literal(lnast->get_name(w));  // `nil` (unbound) parses to nothing
        if (d == nullptr || !d->is_just_i64() || d->to_just_i64() <= 0) {
          return std::nullopt;
        }
        total += d->to_just_i64();
        v      = lnast->get_sibling_next(w);
      }
      return total > 0 && total < (1 << 20) ? std::optional<int>(static_cast<int>(total)) : std::nullopt;
    }
    case N::Lnast_ntype_bit_and: {
      // ONE non-negative operand bounds the AND: `a & C` is in [0, C] whatever
      // `a` is. That is how a one-bit read (`(en & 1)`) and a masked field
      // (`data & _rep_1` on a `u15`) state their window.
      std::optional<int> best;
      for (auto o = c0.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(c0); !o.is_invalid(); o = lnast->get_sibling_next(o)) {
        auto w = known_unsigned_bits(o, walk_depth + 1);
        if (w && (!best || *w < *best)) {
          best = w;
        }
      }
      return best;
    }
    case N::Lnast_ntype_bit_or:
    case N::Lnast_ntype_bit_xor: {
      // OR/XOR need EVERY operand bounded (a negative one sets the high bits).
      int widest = 0;
      for (auto o = c0.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(c0); !o.is_invalid(); o = lnast->get_sibling_next(o)) {
        auto w = known_unsigned_bits(o, walk_depth + 1);
        if (!w) {
          return std::nullopt;
        }
        widest = std::max(widest, *w);
      }
      return widest > 0 ? std::optional<int>(widest) : std::nullopt;
    }
    case N::Lnast_ntype_shl: {
      // `a << k` (constant k) grows the window by exactly k.
      auto src = c0.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(c0);
      auto amt = src.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(src);
      if (amt.is_invalid() || !lnast->get_sibling_next(amt).is_invalid()) {
        return std::nullopt;
      }
      auto        w = known_unsigned_bits(src, walk_depth + 1);
      const auto* k = plain_uint_literal(lnast->get_name(amt));
      if (!w || k == nullptr || !k->is_just_i64()) {
        return std::nullopt;
      }
      const int64_t total = *w + k->to_just_i64();
      return total > 0 && total < (1 << 20) ? std::optional<int>(static_cast<int>(total)) : std::nullopt;
    }
    case N::Lnast_ntype_store: {
      // A pure copy carries its value's window.
      auto val = c0.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(c0);
      if (val.is_invalid() || !lnast->get_sibling_next(val).is_invalid()) {
        return std::nullopt;  // an indexed store writes only part of the target
      }
      return known_unsigned_bits(val, walk_depth + 1);
    }
    default: return std::nullopt;
  }
}

// `x = 0ub????…` (every bit unknown) where `x` is DECLARED exactly that wide is
// the width-taking wildcard spelled the long way: emit `0sb?`. upass fills it
// back to the identical value on re-parse (uPass_runner::resolve_x_fill), so
// this is a pure spelling change — and one that keeps a 1027-bit poison from
// putting 1027 `?` on a line. Only when the width is DECLARED (a port or a `uN`
// declare, both of which the writer emits with their type): without one the
// literal itself is the only statement of the width, and must stay.
int Lnast_prp_writer::x_poison_width(Lnast_nid val_nid) const {
  if (val_nid.is_invalid() || !Lnast_ntype::is_const(lnast->get_type(val_nid))) {
    return 0;
  }
  const auto txt = lnast->get_name(val_nid);
  if (txt.size() < 4 || txt[0] != '0' || txt[1] != 'u' || txt[2] != 'b') {
    return 0;  // `0ub…`: an UNSIGNED all-unknown pattern (a signed one does not fill)
  }
  int w = 0;
  for (const char c : txt.substr(3)) {
    if (c == '_') {
      continue;
    }
    if (c == '?') {
      ++w;
      continue;
    }
    if (c == '0' && w == 0) {
      continue;  // to_pyrope's leading sign digit
    }
    return 0;  // a KNOWN bit: not an all-unknown poison
  }
  return w > 1 ? w : 0;
}

bool Lnast_prp_writer::is_x_poison_of_width(Lnast_nid val_nid, int bits) const {
  return bits > 1 && x_poison_width(val_nid) == bits;
}

std::string Lnast_prp_writer::x_poison_shorthand(Lnast_nid val_nid, std::string_view lhs) const {
  const std::string nm(lhs);
  auto              it = port_bits_.find(nm);
  if (it == port_bits_.end() || !is_x_poison_of_width(val_nid, it->second)) {
    return {};
  }
  // The wildcard fills from the DECLARED width, so it may only replace the
  // literal when the emitted source still states that width — a port's
  // signature, or a `:uN` on the declaration. Otherwise the literal IS the
  // width and has to stay.
  return typed_emitted_.count(nm) != 0 ? std::string("0sb?") : std::string{};
}

std::optional<std::string> Lnast_prp_writer::const_lane_value(Lnast_nid n, int64_t bits) const {
  if (n.is_invalid() || !Lnast_ntype::is_const(lnast->get_type(n))) {
    return std::nullopt;
  }
  const std::string txt(lnast->get_name(n));
  const auto*       d = plain_uint_literal(txt);
  if (d == nullptr) {
    return std::nullopt;  // negative / unknown-bit lane: the window mask is real
  }
  const int w = d->get_bits() > 0 ? d->get_bits() - 1 : 0;
  if (w > bits) {
    return std::nullopt;  // over-wide literal: the window mask is what truncates it
  }
  if (d->is_just_i64() && d->to_just_i64() == 0) {
    return std::string("0");
  }
  return canonical_const_text(txt);
}

void Lnast_prp_writer::note_port_width(std::string_view name, std::string_view type_txt) {
  if (type_txt.size() < 2 || type_txt.front() != 'u') {
    return;
  }
  int w = 0;
  for (char c : type_txt.substr(1)) {
    if (c < '0' || c > '9' || w > 100000) {
      return;
    }
    w = w * 10 + (c - '0');
  }
  if (w > 0) {
    port_bits_.emplace(std::string(name), w);
  }
}

// `x#[3..=3]` is one bit — spell it `x#[3]`.
std::string Lnast_prp_writer::fmt_bit_range(std::string_view s, int lo, int hi) {
  return lo == hi ? std::format("{}#[{}]", s, lo) : std::format("{}#[{}..={}]", s, lo, hi);
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
    case N::Lnast_ntype_tuple_add: {
      // Inline tuple type as carried by Slang struct ports:
      // tuple_add(store(fp,nil,u1), store(addr,nil,u5)) -> `(fp:u1, addr:u5)`.
      // Keeping this shape in the emitted signature makes its dotted body
      // reads declared and lets SSA flatten the same ABI again on recompile.
      Port_group_node root;
      for (auto field = lnast->get_child(type_nid); !field.is_invalid(); field = lnast->get_sibling_next(field)) {
        if (!N::is_store(lnast->get_type(field))) {
          continue;
        }
        auto name_nid = lnast->get_child(field);
        if (name_nid.is_invalid() || !N::is_ref(lnast->get_type(name_nid))) {
          continue;
        }
        auto        init_nid   = lnast->get_sibling_next(name_nid);
        auto        field_type = init_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(init_nid);
        std::string type_text;
        if (!field_type.is_invalid()) {
          type_text = render_type_at(field_type);
        }

        // A nested packed struct may arrive either as a nested tuple_add or as
        // flat stores named `header.id`, `header.kind`. Rebuild the latter into
        // `header:(id:T, kind:U)`; a dotted field spelling inside one tuple is
        // an expression path, not a legal field declaration.
        Port_group_node* node  = &root;
        std::string_view path  = lnast->get_name(name_nid);
        size_t           start = 0;
        for (;;) {
          const auto       dot  = path.find('.', start);
          const bool       last = dot == std::string_view::npos;
          std::string      comp(path.substr(start, last ? std::string_view::npos : dot - start));
          Port_group_node* kid = nullptr;
          for (auto& [name, child] : node->kids) {
            if (name == comp) {
              kid = child.get();
              break;
            }
          }
          if (kid == nullptr) {
            node->kids.emplace_back(comp, std::make_unique<Port_group_node>());
            kid = node->kids.back().second.get();
          }
          if (last) {
            kid->is_leaf   = true;
            kid->type_text = std::move(type_text);
            break;
          }
          node  = kid;
          start = dot + 1;
        }
      }
      auto render = [&](auto&& self, const Port_group_node& node) -> std::string {
        if (node.is_leaf) {
          return node.type_text.empty() ? std::string{} : ":" + node.type_text;
        }
        std::string out   = "(";
        bool        first = true;
        for (const auto& [name, child] : node.kids) {
          if (!first) {
            out += ", ";
          }
          out += quote_kw_path(name);
          if (child->is_leaf) {
            if (!child->type_text.empty()) {
              out += ":" + child->type_text;
            }
          } else {
            out += ":" + self(self, *child);
          }
          first = false;
        }
        return out + ")";
      };
      return render(render, root);
    }
    default: return {};  // comp_type_tuple / named-type ref — not yet serialised; drop
  }
}

// ── assign ────────────────────────────────────────────────────────────────────

void Lnast_prp_writer::index_store_timechecks() {
  store_timechecks_.clear();

  // Each parent is an independent statement sequence.  Track the most recent
  // check by interned RHS id, plus the most recent check on each source line,
  // while walking that sequence once in source order.
  std::function<void(Lnast_nid)> scan_parent = [&](Lnast_nid parent) {
    absl::flat_hash_map<int32_t, Lnast_nid>                                                                  latest;
    absl::flat_hash_map<int32_t, absl::flat_hash_map<std::string, absl::flat_hash_map<uint32_t, Lnast_nid>>> by_line;

    for (auto stmt = lnast->get_child(parent); !stmt.is_invalid(); stmt = lnast->get_sibling_next(stmt)) {
      const auto type = lnast->get_type(stmt);
      if (Lnast_ntype::is_timecheck(type)) {
        auto checked_ref = lnast->get_child(stmt);
        if (!checked_ref.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(checked_ref))) {
          const int32_t name_id = lnast->get_name_id(checked_ref);
          latest[name_id]       = stmt;
          const auto span       = lnast->span_of(stmt);
          if (span.start_line) {
            by_line[name_id][span.file][*span.start_line] = stmt;
          }
        }
      } else if (Lnast_ntype::is_store(type)) {
        auto rhs = lnast->get_child(stmt);
        if (!rhs.is_invalid()) {
          for (auto next = lnast->get_sibling_next(rhs); !next.is_invalid(); next = lnast->get_sibling_next(next)) {
            rhs = next;
          }
        }
        if (!rhs.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(rhs))) {
          const int32_t name_id = lnast->get_name_id(rhs);
          if (auto fallback = latest.find(name_id); fallback != latest.end()) {
            Lnast_nid  check = fallback->second;
            const auto span  = lnast->span_of(stmt);
            if (span.start_line) {
              if (auto nit = by_line.find(name_id); nit != by_line.end()) {
                if (auto fit = nit->second.find(span.file); fit != nit->second.end()) {
                  if (auto lit = fit->second.find(*span.start_line); lit != fit->second.end()) {
                    check = lit->second;
                  }
                }
              }
            }
            store_timechecks_.emplace(stmt.get_class_index().value, check);
          }
        }
      }

      // Nested statements have their own preceding-sibling scope.
      scan_parent(stmt);
    }
  };

  scan_parent(lnast->get_root());
}

std::string Lnast_prp_writer::render_timecheck_suffix(Lnast_nid check) const {
  if (check.is_invalid()) {
    return {};
  }
  auto ref = lnast->get_child(check);
  auto lo  = ref.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(ref);
  auto hi  = lo.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(lo);
  if (lo.is_invalid()) {
    return {};
  }
  const auto lo_txt = lnast->get_name(lo);
  const auto hi_txt = hi.is_invalid() ? lo_txt : lnast->get_name(hi);
  if (lo_txt == "nil" || hi_txt == "nil") {
    return "@[]";
  }
  if (lo_txt == hi_txt) {
    return std::format("@[{}]", lo_txt);
  }
  return std::format("@[{}..={}]", lo_txt, hi_txt);
}

// `store(var, level0..levelN, value)`. 0 levels → `var = value`
// (the old assign spelling, decl keyword preserved); ≥1 level →
// `var[level0]…[levelN] = value` (the old tuple_set spelling). The level-walk
// loop handles both: with no middle siblings it emits just `var = value`.
void Lnast_prp_writer::write_store() {
  const Lnast_nid store_nid = cur;
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
  // Whole ARRAY-to-ARRAY copy (`d = q`, both declared `[N]T`): the recompile
  // has no lowering for a multi-element (tuple) store between memories —
  // expand into per-element copies, which lower as ordinary element ports.
  {
    auto val = lnast->get_sibling_next(first);
    if (!val.is_invalid() && lnast->is_last_child(val) && Lnast_ntype::is_ref(lnast->get_type(val))) {
      auto rhs  = std::string(strip_prefix(lnast->get_name(val)));
      auto dit  = array_decl_size_.find(lhs);
      auto sit2 = array_decl_size_.find(rhs);
      if (dit != array_decl_size_.end() && sit2 != array_decl_size_.end() && dit->second == sit2->second && dit->second > 0
          && dit->second <= 256) {
        for (int64_t k = 0; k < dit->second; ++k) {
          if (k != 0) {
            os << "\n";
            print_indent();
          }
          os << lhs << "[" << k << "] = " << rhs << "[" << k << "]";
        }
        move_to_parent();
        return;
      }
    }
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
  // Second self-store fold, on the RENDERED value.  The raw-name test above only
  // catches `x = x` spelled that way in the LNAST; the common shape is
  // `o = n___ssa_1` where `n___ssa_1 = o` is a single-use temp INLINED right here,
  // so what actually reaches the file is `o = o` — which Pyrope rejects on
  // re-parse ("irrelevant assignment: `o` is assigned to itself"), breaking the
  // v -> prp -> v round trip.  (A Verilog `n = o; … o <= n;` pair collapses to
  // exactly this whenever the if-merge keeps the store as a statement instead of
  // an if-expression.)  Guards: only a NON-declaring store may be dropped (else
  // the name is never defined), and a stage decl re-attaches to its store.
  if (scalar && (is_bundle_field(lhs) || declared_.count(lhs) != 0) && stage_decls_.find(lhs) == stage_decls_.end()
      && render_value(val_nid, /*operand_ctx=*/false) == lhs) {
    move_to_parent();
    return;
  }
  std::string prefix = decl_prefix(lhs);
  print(prefix);
  print(lhs);
  if (scalar && !prefix.empty()) {
    if (auto tit = type_specs_.find(lhs); tit != type_specs_.end() && !tit->second.empty()) {
      typed_emitted_.insert(lhs);
      print(":");
      print(tit->second);
    } else if (auto pit = port_bits_.find(lhs); pit != port_bits_.end() && is_x_poison_of_width(val_nid, pit->second)) {
      // This declaring store's value is a full-width x poison, which is about to
      // print as the `0sb?` wildcard — so the DECLARE has to carry the width the
      // literal used to state, or the re-parse would fill a 1-bit unknown.
      typed_emitted_.insert(lhs);
      print(std::format(":u{}", pit->second));
    }
  }
  std::string tuple_field_path;
  bool        tuple_field_path_ok = true;
  for (auto level = lnast->get_sibling_next(first); !level.is_invalid() && !lnast->is_last_child(level);
       level      = lnast->get_sibling_next(level)) {
    if (lnast->get_type(level) != Lnast_ntype::Lnast_ntype_const) {
      tuple_field_path_ok = false;
      break;
    }
    std::string field(lnast->get_name(level));
    if (field.size() >= 2 && (field.front() == '\'' || field.front() == '"') && field.back() == field.front()) {
      field = field.substr(1, field.size() - 2);
    }
    if (!tuple_field_path.empty()) {
      tuple_field_path += ".";
    }
    tuple_field_path += field;
  }
  const auto quoted_tuple_path = tuple_field_path_ok ? quote_field_path(tuple_field_path) : std::nullopt;
  const bool tuple_field_store = quoted_tuple_path && is_bundle_field(lhs + "." + tuple_field_path);
  while (move_to_sibling() && !is_last_child()) {
    if (!tuple_field_store) {
      print("[");
      print(render_value(cur, /*operand_ctx=*/false));
      print("]");
    }
  }
  if (tuple_field_store) {
    print(".");
    print(*quoted_tuple_path);
  }
  print(" = ");
  // An all-`?` poison whose width IS the target's declared width re-compacts to
  // the wildcard it came from: `x:u48 = 0sb?` says the same thing as 48 `?`, and
  // the re-parse fills it back to the identical value (the declared width is
  // right there in the emitted `mut x:u48`). Without a declared width the long
  // literal IS the width, so it stays.
  if (auto sh = x_poison_shorthand(cur, lhs); !sh.empty()) {
    print(sh);
  } else {
    print(render_value(cur, /*operand_ctx=*/false));  // cursor sits on the value (last child)
  }
  if (auto it = store_timechecks_.find(store_nid.get_class_index().value); it != store_timechecks_.end()) {
    print(render_timecheck_suffix(it->second));
  }
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
  // One LNAST node carries all four verification statements; the obligation
  // KIND rides in a sentinel const child ahead of the optional message (see
  // prp2lnast). Recover it and re-emit the ORIGINAL keyword — writing every
  // kind as `cassert` silently retyped an `assert` into an elaboration check
  // on round-trip, and leaked the raw sentinel into the message slot.
  const std::string cond = render_value(cur, /*operand_ctx=*/false);
  std::string       kw   = "assert";
  std::string       msg;
  if (move_to_sibling()) {
    const std::string_view t = current_text();
    if (t == "__fkind__assert_always") {
      kw = "assert_always";
    } else if (t == "__fkind__assume") {
      kw = "assume";
    } else if (t == "__fkind__assume_nocheck") {
      kw = "assume_nocheck";
    } else if (t == "__fkind__cassert") {
      kw = "cassert";
    } else {
      msg = render_value(cur, /*operand_ctx=*/false);  // no sentinel: this IS the message
    }
    if (msg.empty() && kw != "assert" && move_to_sibling()) {
      msg = render_value(cur, /*operand_ctx=*/false);
    }
  }
  print(kw);
  print("(");
  print(cond);
  if (!msg.empty()) {
    print(", ");
    print(msg);
  }
  print(")");
  move_to_parent();
}

// ── func_call ─────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_func_call() {
  if (!move_to_child()) {
    return;
  }
  // LHS (the result/instance variable)
  const std::string raw_lhs(current_text());
  auto              lhs = std::string(strip_prefix(raw_lhs));
  // function name (next sibling)
  move_to_sibling();
  std::string call_name(unquote_callee(current_text()));
  std::string call_tail = call_name;
  if (auto p = call_tail.rfind('.'); p != std::string::npos) {
    call_tail = call_tail.substr(p + 1);
  }
  // Reference the (possibly aliased) import const for the callee — see the import
  // emission in write_module(): a case-collision with the instance var name forces
  // a non-colliding alias. Look the FULL callee name up (import_alias_ is keyed by it):
  // a dotted Pyrope-origin callee (`ALU.ALU`) maps to its binding `ALU`, so the call
  // prints as `ALU(...)`. A callee with no import (e.g. a same-file ref) is not in the
  // map and stays exactly as before.
  std::string callee_ref = call_name;
  if (auto ait = import_alias_.find(call_name); ait != import_alias_.end()) {
    callee_ref = ait->second;
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
  const bool name_instance = !is_tmp(lhs)
                             && ((instantiated_modules_ != nullptr && instantiated_modules_->count(call_tail) != 0u)
                                 || lnast->has_external_module(call_tail));

  // A zero-output module is a sink instance (DPI/verification observers are
  // the common RTL shape).  Writing `mut inst = Sink(...)` makes the Pyrope
  // reader create a value-result temp plus `inst = temp`; tolg correctly emits
  // no result pin for the sink call, then that synthetic copy resolves the temp
  // to nil.  Emit the language's call-as-statement form instead.  The explicit
  // name attribute still preserves the RTL hierarchy name.
  const bool sink_call = sink_modules_ != nullptr && sink_modules_->count(call_tail) != 0u;
  if (sink_call) {
    if (auto fit = fold_info_.find(raw_lhs); fit != fold_info_.end() && fit->second.use_count != 0) {
      emit_unimplemented(std::format("result '{}' of zero-output module '{}' is read", lhs, call_name));
      move_to_parent();
      return;
    }
  }

  // A multi-output callee (comb or mod) whole-binds like any other instance —
  // `mut r = C(args)` — and its outputs are read as `r.port`.  (The old
  // destructure form `mut (r__o = C.o, …) = C(args)` targeted the runner-inline
  // era; today slang-origin units carry `hdl` and stay Sub instances, where the
  // destructure silently dropped every output binding — the INT2FP fracRounded
  // miscompile.)
  if (!sink_call) {
    print(decl_prefix(lhs));
    print(lhs);
    print(" = ");
  }
  print(quote_module_path(callee_ref));  // a Verilog escaped-id callee needs backticks
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

// rolled_for(ref(index), const(first), const(step), const(count),
//            const(activation), const(next_active), tuple_add(carries),
//            stmts(source_body), stmts(lowering_payload))
//
// Only the source half is public Pyrope. The lowering payload deliberately
// stays invisible here; re-parsing this loop lets the roller rebuild it from
// the same source instead of serializing compiler-reserved call actuals.
void Lnast_prp_writer::write_rolled_for() {
  const auto             rolled = cur;
  std::vector<Lnast_nid> kids;
  for (auto c = lnast->get_child(rolled); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    kids.emplace_back(c);
  }
  if (kids.size() != lnast_rolled_for::arity || !Lnast_ntype::is_ref(lnast->get_type(kids[lnast_rolled_for::index]))
      || !Lnast_ntype::is_stmts(lnast->get_type(kids[lnast_rolled_for::source_body]))) {
    emit_unimplemented("malformed rolled_for transport");
    return;
  }

  int64_t  first = 0;
  int64_t  step  = 0;
  uint64_t count = 0;
  try {
    first = std::stoll(std::string(lnast->get_name(kids[lnast_rolled_for::first])));
    step  = std::stoll(std::string(lnast->get_name(kids[lnast_rolled_for::step])));
    count = std::stoull(std::string(lnast->get_name(kids[lnast_rolled_for::count])));
  } catch (const std::exception&) {
    emit_unimplemented("malformed rolled_for domain");
    return;
  }
  if (step == 0) {
    emit_unimplemented("invalid rolled_for domain");
    return;
  }
  std::optional<int64_t> last;
  if (count != 0) {
    using i128     = __int128;
    const i128 end = static_cast<i128>(first) + static_cast<i128>(count - 1) * step;
    if (end < std::numeric_limits<int64_t>::min() || end > std::numeric_limits<int64_t>::max()) {
      emit_unimplemented("overflowing rolled_for domain");
      return;
    }
    last = static_cast<int64_t>(end);
  }

  print("for ");
  print(strip_prefix(lnast->get_name(kids[lnast_rolled_for::index])));
  print(" in ");
  print(std::to_string(first));
  if (count == 0) {
    // `rolled_for` itself permits a zero-count descriptor. Preserve its
    // source semantics with an empty exclusive range; there is no final
    // ordinal whose inclusive endpoint could be printed.
    print("..<");
    print(std::to_string(first));
  } else {
    print("..=");
    print(std::to_string(*last));
    if (step != 1) {
      print(" step ");
      print(std::to_string(step));
    }
  }
  print(" {\n");
  ++depth;
  nid_stack.push(rolled);
  cur = kids[lnast_rolled_for::source_body];
  write_node();
  cur = nid_stack.top();
  nid_stack.pop();
  --depth;
  print_indent();
  print("}");
  cur = rolled;
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
    // A single contiguous run consumes `ins` WHOLE (`dst#[lo..=hi] = ins`), so a
    // loose expression needs no parens there. Several runs each append a
    // `#[..]` slice to it, which does.
    ins = render_value(cur, /*operand_ctx=*/mask_runs(mask_txt).size() != 1);
  }
  move_to_parent();

  // A set_mask is an in-place RMW.  After SSA stripping, the slang reader's
  // versioned result (`set_mask(OUT___ssa_1, OUT, ..)`) collapses to dst==val,
  // i.e. an in-place write on the base (the redundant `OUT = OUT___ssa_1`
  // store-back then folds away in write_store).  When the base genuinely differs
  // from the source value, copy it in first.
  std::string target   = dst;
  bool        need_sep = false;
  auto        runs     = mask_runs(mask_txt);
  if (dst != val) {
    // The copy is followed by one lane write per run, so the target is assigned
    // TWICE even though the LNAST defines it once — it must not be declared
    // `const` ("const `t` rebind (assigned 2 times)" on recompile).
    if (!runs.empty()) {
      multi_def_tmp_.insert(target);
      single_store_.erase(target);
    }
    print(decl_prefix(target));
    print(target);
    os << std::format(" = {}", val);
    need_sep = true;
  }

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
      os << std::format("{} = {}", fmt_bit_range(target, lo, hi), ins);
    } else {
      os << std::format("{} = {}", fmt_bit_range(target, lo, hi), fmt_bit_range(ins, ins_off, ins_off + w - 1));
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
    // concat( dst, lane_msb, …, lane_lsb ): child0 IS the def. Leaving it out
    // would count the destination as a READ, so every temp feeding a concat
    // looks multiply-used and stops folding (and the concat's own statement
    // would never be recognised as the def of its temp).
    case N::Lnast_ntype_concat:
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

// Operand stability THROUGH the inlines this writer performs.
//
// `operands_stable` looks only at a def's DIRECT operands. That is not enough
// once a chain of single-use temps is folded: the fold moves the whole cone to
// the LAST use, so every name anywhere in the folded cone has to be unchanged
// over the full window, not just the names the outermost node mentions.
//
// The shape that broke: a SystemVerilog `function automatic` returns through a
// variable named after the function, and an unrolled loop calls it once per
// iteration, so the reader emits N writes to that ONE name, each immediately
// consumed by the accumulator (`o |= is_used(states_q[i])`). Every `%t = acc |
// is_used` was adjacent to its use and folded, and then the `o__wN` copies
// folded on top — which slid all N reads of `is_used` down past all N writes.
// vpu_trans came out as `id_trans_busy_o = ((0 | is_used) | is_used) | …`,
// seven reads of the LAST iteration's value: a silent miscompile that
// `//bench:minion_lec` caught as `id_trans_busy_o(ref=1 impl=0)`.
//
// Whether a cone participates is decided by `may_inline_name_id` — the NAME
// policy — and NOT by membership in `foldable_`: analyze_folding fills that set
// from an unordered_map walk while consulting it, so a candidate reached early
// cannot see that a name it reads will itself be folded later, and which of the
// two orders you get depends on string hashing. Treating every single-def temp
// as if it will be folded is conservative — the worst case is one fold declined
// and one extra emitted line.
//
// The wrapper in the header answers the easy cases from two order-independent
// summaries built once per unit by build_stability_index():
// summarize_stability_shape() classifies the cone (cyclic / deeper than 32) and
// stability_hazard_idx_ is the sorted set of write positions that could
// invalidate a moved read. Only when a hazard write actually falls inside the
// (d, u) window does the precise cone walk below run.
bool Lnast_prp_writer::operands_stable_deep(Lnast_nid def_node, int d, int u, uint64_t walk_epoch, int walk_depth) const {
  if (walk_depth > 32) {
    return false;  // pathological chain: decline rather than walk it
  }
  int pos = 0;
  for (auto c = lnast->get_child(def_node); !c.is_invalid(); c = lnast->get_sibling_next(c), ++pos) {
    if (pos == 0) {
      continue;  // the LHS being defined
    }
    if (lnast->get_type(c) != Lnast_ntype::Lnast_ntype_ref) {
      continue;  // const / type leaf — never changes
    }
    const int32_t name_id   = lnast->get_name_id(c);
    const size_t  name_slot = static_cast<size_t>(name_id < 0 ? -static_cast<int64_t>(name_id) : name_id);
    const auto    epoch_tag = walk_epoch << 2;
    const auto    state     = name_slot < stability_walk_state_.size() ? stability_walk_state_[name_slot] : uint64_t{0};
    if (may_inline_name_id(name_id) && state != (epoch_tag | 2)) {
      // The on-stack mark catches a genuine CYCLE; the done mark remembers a
      // cone already verified in this same query. Both are needed: a cone that
      // re-converges (`%d = %b + %c` with `%b`/`%c` both reading `%a` — the
      // normal shape after CSE) is a DAG, not a loop, and a single
      // never-unwound `seen` set rejected it and lost the fold.
      if (state == (epoch_tag | 1)) {
        return false;  // already on the walk: refuse rather than loop
      }
      if (name_slot >= stability_walk_state_.size()) {
        stability_walk_state_.resize(name_slot + 1);
      }
      stability_walk_state_[name_slot] = epoch_tag | 1;
      if (!operands_stable_deep(fold_info_id_.at(name_id)->def_node, d, u, walk_epoch, walk_depth + 1)) {
        return false;
      }
      stability_walk_state_[name_slot] = epoch_tag | 2;
      // Fall through to the write-index check anyway: may_inline_name_id answers
      // "COULD be folded", so a name that ends up emitted still has to hold its
      // own value over the window. (A name that really is folded has its single
      // def before `d`, so the check is a no-op there.)
    }
    auto it = write_idx_id_.find(name_id);
    if (it == write_idx_id_.end()) {
      continue;  // never assigned (io input / const-fed) — stable
    }
    const auto next_write = std::upper_bound(it->second.begin(), it->second.end(), d);
    if (next_write != it->second.end() && *next_write < u) {
      return false;  // operand reassigned between the def and its last use
    }
  }
  return true;
}

// Memoized depth-first walk over the inline dependency graph: for every temp
// that may inline, the depth of the operand chain below it (capped at 33 =
// "too deep to fold") and whether that chain closes on itself.
//
// EXPLICIT STACK, not recursion. The memo makes the work linear (every temp
// expanded once, every operand edge looked at once), but a recursive descent
// would nest one C++ frame per temp of the longest not-yet-memoized chain --
// i.e. its stack depth is set by the DESIGN, exactly what this summary exists to
// cap for render_def_rhs. MEASURED: a 500-deep `((x<<1)|x[i])` temp
// chain put 1,069 frames here and overflowed a 512 KiB worker stack. The frame
// vector below grows on the heap instead, and the walk's stack use is O(1).
//
// Same semantics as the recursive form: `state_ == 1` marks a temp that is on
// the walk (an operand edge back into it is a cycle -- the REACHED temp is
// flagged, and every frame on the way back up inherits the flag through
// `absorb`), `2` a finished one whose depth is in stability_shape_depth_.
uint8_t Lnast_prp_writer::summarize_stability_shape(int32_t name_id) {
  if (auto it = stability_shape_state_.find(name_id); it != stability_shape_state_.end()) {
    if (it->second == 1) {
      stability_shape_cyclic_.insert(name_id);
      return 33;
    }
    return stability_shape_depth_.at(name_id);
  }

  struct Frame {
    int32_t   name_id;
    Lnast_nid next;   // the next operand child of the def still to look at
    uint8_t   depth;  // max over the operands absorbed so far (1 = a leaf)
  };
  std::vector<Frame> stack;
  stack.reserve(64);

  // Enter `id`: mark it on the walk and position its cursor after the dst child.
  const auto push = [&](int32_t id) {
    stability_shape_state_[id] = 1;
    Lnast_nid first;
    if (auto fit = fold_info_id_.find(id); fit != fold_info_id_.end()) {
      first = lnast->get_child(fit->second->def_node);  // child 0 is the dst, skipped
    }
    stack.push_back(Frame{.name_id = id, .next = first.is_invalid() ? first : lnast->get_sibling_next(first), .depth = 1});
  };
  // Fold a resolved operand `dep` (depth `sub`) into `parent`.
  const auto absorb = [&](Frame& parent, int32_t dep, uint8_t sub) {
    if (stability_shape_cyclic_.count(dep) != 0u) {
      stability_shape_cyclic_.insert(parent.name_id);
    }
    parent.depth = std::max<uint8_t>(parent.depth, static_cast<uint8_t>(std::min<int>(33, 1 + sub)));
  };

  push(name_id);
  while (!stack.empty()) {
    bool descended = false;
    {
      Frame& f = stack.back();  // valid until the push below
      while (!f.next.is_invalid()) {
        const auto c = f.next;
        f.next       = lnast->get_sibling_next(c);
        if (lnast->get_type(c) != Lnast_ntype::Lnast_ntype_ref) {
          continue;
        }
        const auto dep = lnast->get_name_id(c);
        if (!may_inline_name_id(dep)) {
          continue;
        }
        if (auto it = stability_shape_state_.find(dep); it != stability_shape_state_.end()) {
          uint8_t sub = 33;
          if (it->second == 1) {
            stability_shape_cyclic_.insert(dep);  // back edge: `dep` is on the walk
          } else {
            sub = stability_shape_depth_.at(dep);
          }
          absorb(f, dep, sub);
          continue;
        }
        push(dep);  // unvisited: descend (may reallocate `stack`; `f` is not used past here)
        descended = true;
        break;
      }
    }
    if (descended) {
      continue;
    }
    // Every operand of the top frame is resolved: finish it and hand its
    // result to the frame that asked for it.
    const Frame done = stack.back();
    stack.pop_back();
    stability_shape_depth_[done.name_id] = done.depth;
    stability_shape_state_[done.name_id] = 2;
    if (!stack.empty()) {
      absorb(stack.back(), done.name_id, done.depth);
    }
  }
  return stability_shape_depth_.at(name_id);
}

bool Lnast_prp_writer::stability_shape_ok(Lnast_nid def_node) const {
  int pos = 0;
  for (auto c = lnast->get_child(def_node); !c.is_invalid(); c = lnast->get_sibling_next(c), ++pos) {
    if (pos == 0 || lnast->get_type(c) != Lnast_ntype::Lnast_ntype_ref) {
      continue;
    }
    const auto dep = lnast->get_name_id(c);
    if (!may_inline_name_id(dep)) {
      continue;
    }
    auto dit = stability_shape_depth_.find(dep);
    if (dit == stability_shape_depth_.end() || dit->second > 32 || stability_shape_cyclic_.count(dep) != 0u) {
      return false;
    }
  }
  return true;
}

void Lnast_prp_writer::build_stability_index() {
  stability_hazard_idx_.clear();
  stability_shape_state_.clear();
  stability_shape_depth_.clear();
  stability_shape_cyclic_.clear();

  for (const auto& [name_id, writes] : write_idx_id_) {
    auto fit = fold_info_id_.find(name_id);
    if (writes.size() > 1 || (writes.size() == 1 && fit != fold_info_id_.end() && fit->second->min_use_index < writes.front())) {
      stability_hazard_idx_.insert(stability_hazard_idx_.end(), writes.begin(), writes.end());
    }
  }
  std::sort(stability_hazard_idx_.begin(), stability_hazard_idx_.end());

  for (const auto& [name_id, fi] : fold_info_id_) {
    (void)fi;
    if (may_inline_name_id(name_id)) {
      summarize_stability_shape(name_id);
    }
  }
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
    const auto next_write = std::upper_bound(it->second.begin(), it->second.end(), d);
    if (next_write != it->second.end() && *next_write < u) {
      return false;  // operand reassigned between the def and its single use
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
        auto tt                                                          = render_type_at(type_nid);
        type_specs_[std::string(strip_prefix(lnast->get_name(var_nid)))] = tt;
        note_port_width(strip_prefix(lnast->get_name(var_nid)), tt);
      }
    }
  }
  // A `uN` DECLARE bounds the variable exactly like a `uN` port does, so a
  // later `x#[0..=N-1]` on it is the same no-op mask (`_rep_1:u15` in a
  // replication lowering is the common one).
  if (t == Lnast_ntype::Lnast_ntype_declare) {
    auto var_nid = lnast->get_child(nid);
    if (!var_nid.is_invalid()) {
      auto type_nid = lnast->get_sibling_next(var_nid);
      if (!type_nid.is_invalid()) {
        note_port_width(strip_prefix(lnast->get_name(var_nid)), render_type_at(type_nid));
      }
      auto qualifier_nid = type_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(type_nid);
      if (!qualifier_nid.is_invalid() && lnast->get_type(qualifier_nid) == Lnast_ntype::Lnast_ntype_const
          && lnast->get_name(qualifier_nid) == "type") {
        type_declared_.insert(std::string(lnast->get_name(var_nid)));
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
      auto&       fi       = fold_info_[nm];
      const auto  name_id  = lnast->get_name_id(c);
      fi.name_id           = name_id;
      const auto name_slot = static_cast<size_t>(name_id < 0 ? -static_cast<int64_t>(name_id) : name_id);
      if (name_slot >= stability_walk_state_.size()) {
        stability_walk_state_.resize(name_slot + 1);
      }
      fold_info_id_[name_id] = &fi;
      name_by_id_[name_id]   = lnast->get_name(c);
      if (def0 && pos == 0) {
        fi.def_count++;
        // A `declare` is not a VALUE def — it introduces the name, the following
        // op/store produces the value. Counting it made every declared-then-
        // assigned name look multi-def, which blocked the single-def fold below.
        fi.decl_defs += (t == Lnast_ntype::Lnast_ntype_declare) ? 1 : 0;
        if (t == Lnast_ntype::Lnast_ntype_declare) {
          // `mut x:u8` NARROWS. Inlining the value at its use would drop the
          // annotation (the declare emits nothing on its own), so remember it.
          if (auto ty = lnast->get_sibling_next(c); !ty.is_invalid() && !render_type_at(ty).empty()) {
            fi.decl_typed = true;
          }
        }
        fi.def_node  = nid;
        fi.def_type  = t;
        fi.def_index = my_index;
        if (t != Lnast_ntype::Lnast_ntype_declare) {
          write_idx_[nm].push_back(my_index);  // pushed in increasing index order
          write_idx_id_[name_id].push_back(my_index);
        }
      } else if (t == Lnast_ntype::Lnast_ntype_func_call && pos == 1 && nm == lnast->get_name(lnast->get_child(nid))) {
        // The CALLEE of `inst = Mod(...)` where the RTL named the instance
        // after its own module -- `br_flow_checks_valid_data_impl
        // br_flow_checks_valid_data_impl (...)`, which is legal SystemVerilog
        // and bedrock-rtl's habit for its assertion sinks. That ref is a MODULE
        // reference, not a read of the call's own result; counting it made a
        // zero-output sink instance look like something consumed its result,
        // and write_func_call then refused to emit the whole module
        // ("result 'X' of zero-output module 'X' is read"). Only the
        // self-named case is skipped, so a genuine higher-order call through a
        // lambda-valued variable still counts its read.
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
    // A REF callee is the bare name. A dotted-import mod/pipe callee instead folds to
    // a quoted string CONST (see unquote_callee), which the is_ref test used to reject
    // outright — so it never registered here, no `import(..)` was emitted for it, and
    // the call printed as the unparseable `'Unit.Entity'(args)`. Register it too, by
    // its unquoted name, so it takes the normal import path.
    if (!callee.is_invalid()) {
      const auto ctype = lnast->get_type(callee);
      if (Lnast_ntype::is_ref(ctype)) {
        // A callee is a module path, not a data ref. strip_prefix() correctly
        // backtick-quotes dotted data names but would turn `file.entity` into
        // one opaque identifier and prevent sibling-module lookup here.
        func_call_callees_.insert(std::string(unquote_callee(lnast->get_name(callee))));
      } else if (const auto cn = lnast->get_name(callee); unquote_callee(cn) != cn) {
        func_call_callees_.emplace(unquote_callee(cn));
      }
    }
    // Record the EXCLUSIVE end of this statement's subtree (the `index` counter
    // right after every child — result, callee, and every argument expression —
    // has been visited), keyed by the result var's raw name.  See the
    // func_call_end_idx_ declaration for why try_inline needs this instead of
    // just `my_index` (the call's own START index).
    if (!fc0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(fc0))) {
      func_call_end_idx_[std::string(lnast->get_name(fc0))] = index;
    }
  }
}

// An LNAST name `<base>___ssa_<N>` is a single-static-assignment VERSION of
// `<base>` (rendered as `<base>__wN`). Multi-assigned signals — and any signal
// with a poison-init `mut x = 0` — get versioned; the version is an internal
// write-once intermediate.
static bool ends_with_ssa_version(std::string_view n) {
  auto p = n.rfind("___ssa_");
  if (p == std::string_view::npos) {
    return false;
  }
  auto digits = n.substr(p + 7);
  return !digits.empty() && std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}
static std::string_view ssa_base(std::string_view n) {
  auto p = n.rfind("___ssa_");
  if (p == std::string_view::npos) {
    return n;
  }
  // strip a leading `%` compiler-temp marker so `%foo___ssa_1` -> `foo`
  auto b = n.substr(0, p);
  if (!b.empty() && b.front() == '%') {
    b = b.substr(1);
  }
  return b;
}

// True when `name_id` names something the writer COULD inline at its use sites
// — i.e. it passes analyze_folding's naming policy (a `%`/`___` compiler temp, a
// firtool `_`-prefixed intermediate, or an `___ssa_N` version) and has exactly
// one definition to inline. analyze_instance_inline's `_t = inst.port` temps are
// covered by the `_` prefix.
//
// Deliberately a NAME test rather than `foldable_id_.contains(name_id)`:
// analyze_folding fills `foldable_` from an unordered walk over `fold_info_`
// while querying it, so the answer for a not-yet-visited name would depend on
// hash order. Being wrong in the "could" direction only costs a conservatively
// declined fold; being wrong the other way slid a read past a write.
bool Lnast_prp_writer::may_inline_name_id(int32_t name_id) const {
  auto fit = fold_info_id_.find(name_id);
  if (fit == fold_info_id_.end() || fit->second->def_count != 1 || fit->second->def_node.is_invalid()) {
    return false;
  }
  auto nit = name_by_id_.find(name_id);
  if (nit == name_by_id_.end() || nit->second.empty()) {
    return false;
  }
  const auto nm = nit->second;
  if (is_tmp(nm) || ends_with_ssa_version(nm)) {
    return true;
  }
  const auto base = ssa_base(nm);
  return !base.empty() && base.front() == '_';
}

void Lnast_prp_writer::compute_dead_signals(Lnast_nid io_nid, Lnast_nid stmts_nid) {
  dead_signals_.clear();
  if (stmts_nid.is_invalid()) {
    return;
  }
  // Names that are externally observable / structurally required — never dropped.
  absl::flat_hash_set<std::string> keep;
  auto                             add_ports = [&](Lnast_nid tup) {
    if (tup.is_invalid()) {
      return;
    }
    for (auto p = lnast->get_child(tup); !p.is_invalid(); p = lnast->get_sibling_next(p)) {
      auto nn = lnast->get_child(p);
      if (!nn.is_invalid()) {
        keep.insert(std::string(strip_prefix(lnast->get_name(nn))));
      }
    }
  };
  auto in_tup = io_nid.is_invalid() ? Lnast_nid{} : lnast->get_child(io_nid);
  add_ports(in_tup);
  add_ports(in_tup.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(in_tup));
  for (const auto& k : folded_keys_) {  // reg/mem vars (have folded flop/mem attrs)
    auto p = k.find('\x01');
    if (p != std::string::npos) {
      keep.insert(k.substr(0, p));
    }
  }
  for (const auto& nm : pin_cone_) {  // clock/reset dependency cone
    keep.insert(nm);
  }
  for (const auto& nm : instance_results_) {
    keep.insert(nm);
  }

  // Precompute every "ancestor prefix" of a name that is actually read
  // (use_count != 0) once — e.g. a read "a.b.c" inserts "a" and "a.b" — so the
  // "does base `s` have any field read" check below is an O(1) set lookup
  // instead of an O(fold_info_) inner rescan FOR EVERY zero-use candidate
  // (was O(N^2): on a design with many thousands of signals — e.g. the
  // XiangShan Backend top — this single function dominated the whole compile).
  read_field_prefixes_.clear();
  auto& read_field_prefixes = read_field_prefixes_;  // populated as a member for bundle reconstruction
  for (const auto& [fname, ffi] : fold_info_) {
    if (ffi.use_count == 0) {
      continue;
    }
    std::string fs(strip_prefix(fname));
    if (!fs.empty() && fs.front() == '`') {  // bundle-field leaves render as `base.field`
      fs = fs.substr(1, fs.size() >= 2 ? fs.size() - 2 : std::string::npos);
    }
    read_field_prefixes.insert(fs);  // the whole name (a base read as a whole counts too)
    for (size_t dot = fs.find('.'); dot != std::string::npos; dot = fs.find('.', dot + 1)) {
      read_field_prefixes.insert(fs.substr(0, dot));
    }
  }

  for (const auto& [name, fi] : fold_info_) {
    if (fi.use_count != 0 || fi.def_count < 1) {
      continue;  // read somewhere, or never defined
    }
    std::string s(strip_prefix(name));
    if (s.find('.') != std::string::npos) {
      continue;  // a bundle-field leaf: leave bundle reconstruction alone
    }
    if (keep.count(s)) {
      continue;
    }
    // A whole-bundle write (`bundle = tuple`) writes ALL of the base's fields, so
    // the base name itself reads 0 times — but if any FIELD `bundle.X` is read
    // (e.g. a later pack `(bundle.a<<N)|...`), the store is NOT dead. Dropping it
    // would lose the bundle copy and leave every field at its `=0` default — a
    // real miscompile (e.g. Dispatcher's `io_out_0_bits_ctrl_0 = io_in_bits_ctrl_0`
    // becoming all-zero), which LEC then refutes against the (correct) cgen output.
    if (read_field_prefixes.count(s)) {
      continue;
    }
    dead_signals_.insert(s);
  }
  if (dead_signals_.empty()) {
    return;
  }
  // Mark every def-statement (declare + pure dataflow assign) of a dead signal so
  // the existing folded-node skip drops it. func_call (instance) statements are
  // kept — instantiation has side effects even if its result is unread.
  std::function<void(Lnast_nid)> mark = [&](Lnast_nid n) {
    for (auto c = lnast->get_child(n); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      const auto t = lnast->get_type(c);
      if (defines_child0(t) && t != Lnast_ntype::Lnast_ntype_func_call) {
        auto c0 = lnast->get_child(c);
        if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0))
            && dead_signals_.count(std::string(strip_prefix(lnast->get_name(c0))))) {
          folded_node_.insert(c.get_class_index().value);
        }
      }
      mark(c);
    }
  };
  mark(stmts_nid);
}

Lnast_nid Lnast_prp_writer::arm_value_def(Lnast_nid stmts_node, std::string expect, std::string& out_lhs) const {
  if (stmts_node.is_invalid() || lnast->get_type(stmts_node) != Lnast_ntype::Lnast_ntype_stmts) {
    return Lnast_nid{};
  }
  // The arm's LAST statement is the value-def to x; any PRECEDING statements must
  // be foldable-temp defs (e.g. `%7 = a & b` feeding `store x = %7`), which are
  // inlined into the value when render_def_rhs spells the final RHS — so they need
  // not be emitted. A preceding non-foldable / non-temp statement means the arm
  // does real extra work and is NOT a pure mux arm.
  std::vector<Lnast_nid> ss;
  for (auto cc = lnast->get_child(stmts_node); !cc.is_invalid(); cc = lnast->get_sibling_next(cc)) {
    ss.push_back(cc);
  }
  if (ss.empty()) {
    return Lnast_nid{};
  }
  for (size_t k = 0; k + 1 < ss.size(); ++k) {
    const auto pt = lnast->get_type(ss[k]);
    if (!defines_child0(pt)) {
      return Lnast_nid{};
    }
    auto px = lnast->get_child(ss[k]);
    if (px.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(px)) || !is_foldable(std::string(lnast->get_name(px)))) {
      return Lnast_nid{};  // a preceding stmt whose result is NOT inlined would be lost
    }
  }
  auto       c = ss.back();
  const auto t = lnast->get_type(c);
  // Must be a render_def_rhs-able value def (store copy or an infix/unary/select
  // op). Exclude statement-forms / control flow / instances.
  if (!defines_child0(t) || t == Lnast_ntype::Lnast_ntype_func_call || t == Lnast_ntype::Lnast_ntype_attr_set
      || t == Lnast_ntype::Lnast_ntype_set_mask || t == Lnast_ntype::Lnast_ntype_range || t == Lnast_ntype::Lnast_ntype_declare
      || t == Lnast_ntype::Lnast_ntype_delay_assign || Lnast_ntype::is_if_like(t) || t == Lnast_ntype::Lnast_ntype_tuple_add) {
    return Lnast_nid{};
  }
  auto x0 = lnast->get_child(c);
  if (x0.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(x0))) {
    return Lnast_nid{};
  }
  // A 3-child `store(mem, idx, val)` is a PER-ENTRY memory write, not a scalar
  // value-def: folding it into a mux arm would render just the value and drop
  // the index (mem_whole_coexist). A type-tailed decl-store keeps its old
  // acceptance (the tail is a type node, not an index).
  if (t == Lnast_ntype::Lnast_ntype_store) {
    if (auto v1 = lnast->get_sibling_next(x0); !v1.is_invalid()) {
      if (auto v2 = lnast->get_sibling_next(v1); !v2.is_invalid() && !Lnast_ntype::is_type(lnast->get_type(v2))) {
        return Lnast_nid{};
      }
    }
  }
  std::string nm(strip_prefix(lnast->get_name(x0)));
  if (nm.find('.') != std::string::npos) {
    return Lnast_nid{};  // scalar only (a bundle leaf keeps its own form)
  }
  if (!expect.empty() && nm != expect) {
    return Lnast_nid{};
  }
  out_lhs = nm;
  return c;
}

// Find if/unique-if nodes that are pure muxes of one scalar and record them for
// write_if to render as a single conditional-expression assignment.
void Lnast_prp_writer::analyze_muxes(Lnast_nid stmts_nid) {
  if (stmts_nid.is_invalid()) {
    return;
  }
  // Top-level `mut` declares, by target — a mux target's poison declare
  // (`mut x:T = 0`) is dead once x is assigned unconditionally by the mux, so it
  // is folded into the mux assignment (`mut x:T = if…`).
  absl::flat_hash_map<std::string, Lnast_nid> top_decl_node;
  for (auto c = lnast->get_child(stmts_nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (lnast->get_type(c) != Lnast_ntype::Lnast_ntype_declare) {
      continue;
    }
    auto v = lnast->get_child(c);
    if (!v.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(v))) {
      top_decl_node.emplace(std::string(strip_prefix(lnast->get_name(v))), c);
    }
  }
  absl::flat_hash_set<std::string> post_dead;  // signals orphaned by a suppressed default store
  std::function<void(Lnast_nid)>   rec = [&](Lnast_nid blk) {
    std::vector<Lnast_nid> kids;
    for (auto c = lnast->get_child(blk); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      kids.push_back(c);
    }
    for (size_t i = 0; i < kids.size(); ++i) {
      const auto c = kids[i];
      if (Lnast_ntype::is_if_like(lnast->get_type(c))) {
        // children: [cond, stmts, cond, stmts, …, (else_stmts)]
        std::vector<Lnast_nid> ic;
        for (auto cc = lnast->get_child(c); !cc.is_invalid(); cc = lnast->get_sibling_next(cc)) {
          ic.push_back(cc);
        }
        const bool   has_else = (ic.size() % 2) == 1;
        const size_t npairs   = ic.size() / 2;
        Mux_info     mi;
        mi.unique = Lnast_ntype::is_unique_if(lnast->get_type(c));
        bool ok   = npairs >= 1;
        for (size_t p = 0; ok && p < npairs; ++p) {
          std::string lhs;
          auto        def = arm_value_def(ic[2 * p + 1], mi.lhs, lhs);
          if (def.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(ic[2 * p]))) {
            ok = false;
            break;
          }
          if (mi.lhs.empty()) {
            mi.lhs = lhs;
          }
          mi.arms.push_back({ic[2 * p], def});
        }
        if (ok) {
          std::string dummy;
          if (has_else) {
            mi.else_def = arm_value_def(ic.back(), mi.lhs, dummy);
            if (mi.else_def.is_invalid()) {
              ok = false;
            }
          } else if (i > 0) {
            // A preceding sibling must be the unconditional default
            // `store lhs = D`; it becomes the else value and is dropped.
            // slang's driver-topological emission can interleave UNRELATED
            // statements (poison stores of other nets) between the seed and
            // the if — the firtool ternary `x = D; if c { x = V }` then missed
            // the collapse and kept the `mut x:T = 0` declare + statement-if
            // (the ResetGen `_mux_1` shape). Scan a bounded window backward;
            // skipping an intervening statement is sound only when it neither
            // READS lhs (it would observe the seed value the fold removes from
            // that position) nor WRITES anything the seed's rhs reads (the
            // else value is evaluated at the if once folded).
            constexpr size_t                 kSeedScanWindow = 32;
            absl::flat_hash_set<std::string> iv_writes;
            for (size_t back = 1; back <= i && back <= kSeedScanWindow; ++back) {
              const auto s = kids[i - back];
              const auto t = lnast->get_type(s);
              if (t == Lnast_ntype::Lnast_ntype_stmts || Lnast_ntype::is_if_like(t) || !defines_child0(t)) {
                break;  // control flow / non-def statement — a fold barrier
              }
              auto x0 = lnast->get_child(s);
              if (x0.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(x0))) {
                break;
              }
              const std::string sname(strip_prefix(lnast->get_name(x0)));
              if (sname == mi.lhs) {
                // Nearest def of lhs decides either way: validate the same
                // scalar value-def shape the adjacent case required. A 3-child
                // `store(mem, idx, val)` is a PER-ENTRY memory write — a
                // partial write can never be the unconditional default
                // (folding one clobbered the whole array in mem_whole_coexist).
                bool indexed_store = false;
                if (t == Lnast_ntype::Lnast_ntype_store) {
                  if (auto v1 = lnast->get_sibling_next(x0); !v1.is_invalid()) {
                    if (auto v2 = lnast->get_sibling_next(v1); !v2.is_invalid() && !Lnast_ntype::is_type(lnast->get_type(v2))) {
                      indexed_store = true;
                    }
                  }
                }
                if (!indexed_store && t != Lnast_ntype::Lnast_ntype_func_call && t != Lnast_ntype::Lnast_ntype_attr_set
                    && t != Lnast_ntype::Lnast_ntype_set_mask && t != Lnast_ntype::Lnast_ntype_range
                    && t != Lnast_ntype::Lnast_ntype_declare) {
                  absl::flat_hash_set<std::string> seed_reads;
                  collect_driver_reads(s, seed_reads);
                  bool clash = false;
                  for (const auto& w : iv_writes) {
                    if (seed_reads.count(w)) {
                      clash = true;
                      break;
                    }
                  }
                  if (!clash) {
                    mi.else_def = s;
                  }
                }
                break;
              }
              iv_writes.insert(sname);
              absl::flat_hash_set<std::string> sreads;
              collect_driver_reads(s, sreads);
              if (sreads.count(mi.lhs)) {
                break;  // an in-between reader of the seed value — cannot fold past it
              }
            }
            if (mi.else_def.is_invalid()) {
              ok = false;
            }
          } else {
            ok = false;  // no else, no preceding default
          }
        }
        // An ARM that READS the target itself (`x = (x & ~m) | v`, the RMW
        // shape) cannot collapse: in the original order that read observed the
        // preceding default's value; a single conditional assignment makes it
        // read the seed — or its own not-yet-complete declaration once
        // fold_decl merges the declare in (`mut x = if c { x & … }`).
        if (ok) {
          for (const auto& arm : mi.arms) {
            absl::flat_hash_set<std::string> rr;
            collect_driver_reads(arm.def, rr);
            if (rr.count(mi.lhs) != 0u) {
              ok = false;
              break;
            }
          }
        }
        const bool seed_is_sibling = ok && !has_else && !mi.else_def.is_invalid();
        if (ok && !mi.else_def.is_invalid()) {
          // success: record + suppress the preceding default store if it is the else.
          if (seed_is_sibling) {
            folded_node_.insert(mi.else_def.get_class_index().value);
          } else if (i > 0) {
            // has explicit else: an immediately-preceding redundant store to lhs is dead.
            auto       prev = kids[i - 1];
            const auto t    = lnast->get_type(prev);
            auto       x0   = lnast->get_child(prev);
            if (defines_child0(t) && !x0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(x0))
                && std::string(strip_prefix(lnast->get_name(x0))) == mi.lhs && t != Lnast_ntype::Lnast_ntype_func_call
                && !Lnast_ntype::is_if_like(t)) {
              folded_node_.insert(prev.get_class_index().value);
              // If that store was a plain copy `lhs = Y` and Y's ONLY read was it
              // (use_count==1), Y is now dead — its sole consumer is gone. Targeted
              // & safe: we know exactly which read disappeared. Skip regs/mems.
              if (t == Lnast_ntype::Lnast_ntype_store) {
                auto yv = lnast->get_sibling_next(x0);
                if (!yv.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(yv)) && lnast->get_sibling_next(yv).is_invalid()) {
                  std::string yn(lnast->get_name(yv));
                  std::string ys(strip_prefix(yn));
                  auto        fit = fold_info_.find(yn);
                  if (fit != fold_info_.end() && fit->second.use_count == 1 && !folded_attrs_.count(ys)
                      && ys.find('.') == std::string::npos) {
                    post_dead.insert(ys);
                  }
                }
              }
            }
          }
          // Fold the target's poison declare (`mut x:T = 0`) into the mux assign:
          // x is now assigned unconditionally by the mux, so the placeholder init
          // is dead. Only a plain value-less `mut` declare with no reg/mem attrs.
          if (auto dit = top_decl_node.find(mi.lhs);
              dit != top_decl_node.end() && !folded_node_.count(dit->second.get_class_index().value) && !folded_attrs_.count(mi.lhs)
              && find_stages_child(dit->second).is_invalid()) {
            auto       d         = dit->second;
            auto       vr        = lnast->get_child(d);                                          // ref
            auto       ty        = vr.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(vr);  // type
            auto       kwn       = ty.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(ty);  // const(kw)
            auto       val       = kwn.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(kwn);
            const bool plain_mut = !kwn.is_invalid() && lnast->get_name(kwn) == "mut";
            if (plain_mut && val.is_invalid()) {  // value-less `mut x:T` (write_declare would emit `= 0`)
              mi.fold_decl = true;
              if (!ty.is_invalid()) {
                mi.decl_type = render_type_at(ty);
              }
              folded_node_.insert(d.get_class_index().value);
            }
          }
          mux_info_.emplace(c.get_class_index().value, std::move(mi));
        }
      }
      rec(c);  // recurse nested scopes
    }
  };
  rec(stmts_nid);

  // Drop signals orphaned by a suppressed default store (their sole read is gone).
  if (!post_dead.empty()) {
    dead_signals_.insert(post_dead.begin(), post_dead.end());
    std::function<void(Lnast_nid)> mark = [&](Lnast_nid n) {
      for (auto c = lnast->get_child(n); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
        const auto t = lnast->get_type(c);
        if (defines_child0(t) && t != Lnast_ntype::Lnast_ntype_func_call) {
          auto c0 = lnast->get_child(c);
          if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0))
              && post_dead.count(std::string(strip_prefix(lnast->get_name(c0))))) {
            folded_node_.insert(c.get_class_index().value);
          }
        }
        mark(c);
      }
    };
    mark(stmts_nid);
  }
}

std::string Lnast_prp_writer::render_mux_expr(const Mux_info& mi) {
  std::string s;
  for (size_t k = 0; k < mi.arms.size(); ++k) {
    s += (k == 0 ? (mi.unique ? "unique if " : "if ") : " elif ");
    s += render_value(mi.arms[k].cond, /*operand_ctx=*/false);
    s += " { " + render_def_rhs(mi.arms[k].def, /*operand_ctx=*/false) + " }";
  }
  s += " else { " + render_def_rhs(mi.else_def, /*operand_ctx=*/false) + " }";
  return s;
}

void Lnast_prp_writer::analyze_expr_inlines(Lnast_nid io_nid, Lnast_nid stmts_nid) {
  (void)io_nid;
  bool_inline_.clear();
  value_inline_.clear();
  if (stmts_nid.is_invalid()) {
    return;
  }
  // Names read by any top-level DECLARE: declares emit before every store, so
  // inlining a later-defined value into one would reorder reads — excluded.
  absl::flat_hash_set<std::string> decl_reads;
  for (auto c = lnast->get_child(stmts_nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (Lnast_ntype::is_declare(lnast->get_type(c))) {
      collect_driver_reads(c, decl_reads);  // excludes child0 — the declared name itself
    }
  }
  // Names an attr_set VALUE references (`[clock_pin=ref gclk__w1]`): attr
  // values render as bare names (render_attr_value), outside the render_value
  // inlining — such a name must keep its def.
  std::function<void(Lnast_nid)> scan_attr_refs = [&](Lnast_nid n) {
    for (auto c = lnast->get_child(n); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (lnast->get_type(c) == Lnast_ntype::Lnast_ntype_attr_set) {
        auto a0 = lnast->get_child(c);
        for (auto v = a0.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(a0); !v.is_invalid();
             v      = lnast->get_sibling_next(v)) {
          if (Lnast_ntype::is_ref(lnast->get_type(v))) {
            decl_reads.insert(std::string(strip_prefix(lnast->get_name(v))));
          }
        }
      }
      scan_attr_refs(c);
    }
  };
  scan_attr_refs(stmts_nid);
  auto scalar_store_val = [&](Lnast_nid store) -> Lnast_nid {
    auto c0 = lnast->get_child(store);
    if (c0.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(c0))) {
      return Lnast_nid{};
    }
    auto v = lnast->get_sibling_next(c0);
    return (!v.is_invalid() && lnast->is_last_child(v)) ? v : Lnast_nid{};
  };
  // `_b2i_N` → unsigned(cond). Mux temporaries deliberately keep their own
  // conditional-expression assignment. Looking for a later consumer used to
  // scan every store once per mux, making generated RTL quadratic to emit.
  for (const auto& [key, mi] : mux_info_) {
    std::string lhs(strip_prefix(mi.lhs));
    auto        fit = fold_info_.find(mi.lhs);
    if (fit == fold_info_.end() || fit->second.use_count != 1 || decl_reads.count(lhs) != 0u) {
      continue;
    }
    if (mi.else_def.is_invalid() || mi.arms.empty()) {
      continue;
    }
    if (lhs.rfind("_b2i", 0) == 0 && mi.arms.size() == 1) {
      auto is_const_val = [&](Lnast_nid def, std::string_view want) {
        auto v = scalar_store_val(def);
        return !v.is_invalid() && lnast->get_type(v) == Lnast_ntype::Lnast_ntype_const
               && std::string_view(lnast->get_name(v)) == want;
      };
      if (is_const_val(mi.arms[0].def, "1") && is_const_val(mi.else_def, "0")) {
        bool_inline_.emplace(lhs, mi.arms[0].cond);
        folded_node_.insert(key);
        continue;
      }
    }
  }
  // Reader-SSA `<base>__wN` single-def single-use scalar store → its value.
  for (const auto& [snode, sidx] : store_nodes_) {
    if (lnast->get_parent(snode).get_class_index().value != stmts_nid.get_class_index().value) {
      continue;  // only an UNCONDITIONAL top-level def is position-independent
    }
    auto v = scalar_store_val(snode);
    if (v.is_invalid()) {
      continue;
    }
    auto        c0 = lnast->get_child(snode);
    std::string raw(lnast->get_name(c0));
    std::string nm(strip_prefix(raw));
    auto        wp = nm.rfind("__w");
    if (wp == std::string::npos || wp + 3 >= nm.size()
        || !std::all_of(nm.begin() + static_cast<std::ptrdiff_t>(wp) + 3, nm.end(), [](unsigned char ch) {
             return std::isdigit(ch);
           })) {
      continue;
    }
    auto fit = fold_info_.find(raw);
    if (fit == fold_info_.end() || fit->second.def_count != 1 || fit->second.use_count != 1 || decl_reads.count(nm) != 0u) {
      continue;
    }
    const auto vt = lnast->get_type(v);
    if (!Lnast_ntype::is_ref(vt) && vt != Lnast_ntype::Lnast_ntype_const) {
      continue;
    }
    // The value moves to the USE site, so everything it reads — including the
    // cone of any temp folded into it — must be unchanged in between. Without
    // this the rule slid a folded `acc | is_used` past six reassignments of
    // `is_used` (see operands_stable_deep).
    if (!operands_stable_deep(snode, fit->second.def_index, fit->second.use_index)) {
      continue;
    }
    value_inline_.emplace(nm, v);
    folded_node_.insert(snode.get_class_index().value);
  }
}

void Lnast_prp_writer::analyze_folding() {
  fold_info_.clear();
  fold_info_id_.clear();
  func_call_callees_.clear();
  write_idx_.clear();
  write_idx_id_.clear();
  name_by_id_.clear();
  stability_walk_state_.clear();
  stability_walk_epoch_ = 0;
  node_read_ids_cache_.clear();
  stripped_name_cache_.clear();
  func_call_end_idx_.clear();
  foldable_.clear();
  foldable_id_.clear();
  folded_node_.clear();
  range_lohi_.clear();
  get_mask_nodes_.clear();
  tuple_get_nodes_.clear();
  store_nodes_.clear();
  type_specs_.clear();
  type_declared_.clear();
  stage_decls_.clear();

  int index = 0;
  scan_node(lnast->get_root(), index);
  build_stability_index();

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

  // Select the single-def names whose value-producing definition can be inlined
  // back into its use(s). Policy (mirrors the inou.cgen Verilog "don't materialise
  // a bare bar[x]" rule):
  //   * EXPRESSION def (has operator work): inline only a `%`/`___` compiler temp,
  //     at a SINGLE use (duplicating arbitrary logic is not worth it).
  //   * PURE INDEX/SELECT def (tuple_get `a[i]` or a bit-slice `get_mask`,
  //     `a#[lo..=hi]` — no operator):
  //       - a TEMP (`%`/`___`, or a firtool `_`-prefixed intermediate, incl. its
  //         `___ssa_N` SSA versions): inline ALWAYS — a meaningless temp should
  //         never get its own line.
  //       - a REAL named signal that is an SSA version (`base___ssa_N`): inline at
  //         up to TWO uses (readable cleanup; SSA-version reads are all inlined,
  //         incl. the one that resolves the base/port, so it stays correct).
  //       - a BARE real name (no SSA version) is NEVER folded: it may be a module
  //         port / reg / output whose externally-visible driver must remain.
  for (auto& [name, fi] : fold_info_) {
    if (fi.def_count - fi.decl_defs != 1) {
      continue;  // must be written exactly once (a bare `declare` is not a write)
    }
    // A `:T` annotation (on the declare or a standalone type_spec) and a
    // `stage[N]` depth both render on the DECLARATION, which inlining deletes —
    // and a narrowing type changes the value. Keep those names on their own line.
    if (fi.decl_typed || type_specs_.count(std::string(strip_prefix(name))) != 0u
        || stage_decls_.count(std::string(strip_prefix(name))) != 0u) {
      continue;
    }
    const bool pure_index = (fi.def_type == Lnast_ntype::Lnast_ntype_tuple_get || fi.def_type == Lnast_ntype::Lnast_ntype_get_mask);
    const bool ssa_ver    = ends_with_ssa_version(name);
    const auto base       = ssa_base(name);
    const bool temp_like  = is_tmp(name) || (!base.empty() && base.front() == '_');

    int max_uses;
    if (pure_index) {
      if (temp_like) {
        max_uses = std::numeric_limits<int>::max();  // temp index/select: always inline
      } else if (ssa_ver) {
        max_uses = 2;  // real SSA-version index/select: inline at <=2 uses
      } else {
        continue;  // bare real name (possible port/reg/output) — keep
      }
    } else {
      // EXPRESSION def: a compiler temp only. `%`/`___` are the raw spellings;
      // `t<N>` / `tt<N>_<M>` are what THIS writer mints for them, and they come
      // back as ordinary source names whenever a generated file is compiled
      // again (a Verilog->Pyrope tree re-read, which is the normal flow) — 8043
      // single-use expressions in one minion emit sat on their own line for
      // exactly that reason. A port/export keeps its driver either way.
      if (!is_tmp(name) && !(is_writer_temp_name(strip_prefix(name)) && !is_pub_export(strip_prefix(name)))) {
        continue;
      }
      max_uses = 1;
    }

    if (fi.use_count < 1 || fi.use_count > max_uses) {
      continue;
    }
    if (fi.def_index < 0 || fi.def_index >= fi.min_use_index) {
      continue;  // need a forward def-before-FIRST-use
    }
    bool ok = is_foldable_optype(fi.def_type);
    if (fi.def_type == Lnast_ntype::Lnast_ntype_store) {
      ok = is_pure_copy(fi.def_node);  // a bare copy `___t = x`
    }
    if (!ok) {
      continue;
    }
    // operands must be stable from the def through the LAST use (use_index), so an
    // N-use inline reads the same operand values at every site. (In SSA every
    // operand is itself write-once, so this is the common-case fast path.)
    if (!operands_stable_deep(fi.def_node, fi.def_index, fi.use_index)) {
      continue;
    }
    foldable_.insert(name);
    foldable_id_.insert(fi.name_id);
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
  // not a single-port read `___x = inst["port"]` (the port string may be a
  // flattened nested path such as `rsp.header.id`) of a known instance result.
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
    // The EXCLUSIVE end of the instantiation statement's own subtree (covers the
    // result, callee, AND every argument expression) — NOT just the call's start
    // index. A read embedded in the instance's OWN argument list (e.g. a port
    // wired straight to the instance's own output, same statement) sorts AFTER
    // the call's start index in pre-order (it is one of the call's descendants)
    // but is still strictly inside [start, end): comparing against the start
    // alone would misclassify it as "after the declaration" and inline an
    // `inst.port` read of an instance that does not exist yet on that line.
    auto eit          = func_call_end_idx_.find(std::string(lnast->get_name(inst)));
    int  inst_def_end = (eit != func_call_end_idx_.end()) ? eit->second : bwit->second.front() + 1;

    std::string raw_name(lnast->get_name(c0));
    std::string tname(strip_prefix(raw_name));
    // `_t` is written exactly once — by THIS def (its `wire` declare is not a
    // write, so write_idx_ excludes it).
    auto        twit = write_idx_.find(raw_name);
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
    if (fit->second.min_use_index < inst_def_end) {
      return;  // a use precedes (or is INSIDE) the instance decl -> genuine
               // feedback (incl. self-reference within its own arg list), keep wire
    }
    // render_value inlines `fold_info_[name].def_node`; point it at this def (a
    // trailing `wire` declare may otherwise be the recorded def).
    fit->second.def_node = def_node;
    fit->second.def_type = def_type;
    foldable_.insert(raw_name);                             // render_value inlines `inst.port` at each use
    folded_node_.insert(def_node.get_class_index().value);  // skip the extraction statement
    suppress_decl_.insert(tname);                           // drop any in-place declare
    instance_output_inlined_.insert(tname);                 // drop the hoisted `wire`/`mut`
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
    return canonical_const_text(text);
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
    std::string sp(strip_prefix(nm));
    if (auto bit = bool_inline_.find(sp); bit != bool_inline_.end()) {
      return "unsigned(" + render_value(bit->second, /*operand_ctx=*/false) + ")";
    }
    if (auto vit = value_inline_.find(sp); vit != value_inline_.end()) {
      return render_value(vit->second, operand_ctx);
    }
    if (is_foldable(nm)) {
      return render_def_rhs(fold_info_.at(nm).def_node, operand_ctx);
    }
    return sp;
  }
  if (t == Lnast_ntype::Lnast_ntype_const) {
    auto txt = const_text(node);
    // A NEGATIVE literal as an infix operand must be parenthesized: the
    // grammar cannot parse `a < -128` (the `< -` sequence mis-lexes), while
    // `a < (-128)` is fine. Parens around a literal are always inert.
    if (operand_ctx && !txt.empty() && txt[0] == '-') {
      return "(" + txt + ")";
    }
    return txt;
  }
  // A non-leaf operand (not produced by the flattened LNAST, but be safe).
  return render_def_rhs(node, operand_ctx);
}

std::string Lnast_prp_writer::wrap_operand(std::string s, bool operand_ctx, bool loose) {
  return (operand_ctx && loose) ? "(" + s + ")" : s;  // parens only where precedence needs them
}

// The dispatcher is deliberately THIN. It is the recursive spine of expression
// rendering (render_def_rhs -> render_value -> render_def_rhs, one level per
// folded single-use temp), and at -O0 every local of every case of one big
// function is live for the whole call: before the split this frame was 13.8 KiB,
// and 34 levels of it overflowed a worker thread on CVA6. The heavyweight
// cases live in render_*_rhs below so only the case actually taken pays for
// its locals.
std::string Lnast_prp_writer::render_def_rhs(Lnast_nid def, bool operand_ctx) {
  using N = Lnast_ntype;
  auto t  = lnast->get_type(def);
  auto c0 = lnast->get_child(def);

  // Infix arithmetic / bitwise / logical / comparison: `a <op> b [<op> c …]`.
  if (auto sym = infix_symbol(t); !sym.empty()) {
    return render_infix_rhs(def, t, sym, operand_ctx);
  }

  switch (t) {
    case N::Lnast_ntype_log_not:
    case N::Lnast_ntype_bit_not: {
      auto        opnd  = lnast->get_sibling_next(c0);
      std::string s     = (t == N::Lnast_ntype_log_not) ? "not " : "~";
      s                += opnd.is_invalid() ? std::string{} : render_value(opnd, /*operand_ctx=*/true);
      return wrap_operand(s, operand_ctx, /*loose=*/true);
    }
    case N::Lnast_ntype_sext: {
      auto        src = lnast->get_sibling_next(c0);
      auto        pos = src.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(src);
      std::string s   = src.is_invalid() ? std::string{} : render_value(src, /*operand_ctx=*/true);
      std::string p   = pos.is_invalid() ? std::string("0") : std::string(strip_prefix(lnast->get_name(pos)));
      return std::format("{}#sext[0..={}]", s, p);  // postfix — binds tight, never wrapped
    }
    case N::Lnast_ntype_get_mask : return render_get_mask_rhs(c0, operand_ctx);
    case N::Lnast_ntype_concat   : return render_concat_rhs(c0, operand_ctx);
    case N::Lnast_ntype_tuple_get: return render_tuple_get_rhs(c0);
    case N::Lnast_ntype_attr_get : {
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

// Infix arithmetic / bitwise / logical / comparison: `a <op> b [<op> c …]`.
std::string Lnast_prp_writer::render_infix_rhs(Lnast_nid def, Lnast_ntype::Lnast_ntype_int t, std::string_view sym,
                                               bool operand_ctx) {
  const bool assoc       = is_associative_optype(t);
  // A same-operator associative operand is inlined WITHOUT its own parens so the
  // chain stays flat: `a op b op c …` instead of `((a op b) op c) …`.  Expand
  // such operands ITERATIVELY via an explicit worklist — a deep associative
  // chain (e.g. a 4096-wide reduction in a fully-unrolled decoder) would
  // overflow the stack if flattened by recursion.  Non-same-op operands render
  // through render_value (whose own recursion is bounded by expression nesting
  // depth, not chain length).
  auto       operands_of = [&](Lnast_nid d, std::vector<Lnast_nid>& rev_stack) {
    std::vector<Lnast_nid> ops;
    for (auto c = lnast->get_sibling_next(lnast->get_child(d)); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      ops.push_back(c);
    }
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {  // reverse-push: back() pops leftmost
      rev_stack.push_back(*it);
    }
  };
  std::vector<std::string> parts;
  std::vector<Lnast_nid>   work;
  operands_of(def, work);
  while (!work.empty()) {
    auto c = work.back();
    work.pop_back();
    bool expanded = false;
    if (assoc && lnast->get_type(c) == Lnast_ntype::Lnast_ntype_ref) {
      std::string nm(lnast->get_name(c));
      if (is_foldable(nm)) {
        auto fdef = fold_info_.at(nm).def_node;
        if (lnast->get_type(fdef) == t) {
          operands_of(fdef, work);  // splice the same-op chain in place
          expanded = true;
        }
      }
    }
    if (!expanded) {
      parts.push_back(render_value(c, /*operand_ctx=*/true));
    }
  }
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) {
      out += " ";
      out += sym;
      out += " ";
    }
    out += parts[i];
  }
  return wrap_operand(out, operand_ctx, /*loose=*/true);
}

std::string Lnast_prp_writer::render_get_mask_rhs(Lnast_nid c0, bool operand_ctx) {
  using N     = Lnast_ntype;
  auto src    = lnast->get_sibling_next(c0);
  auto mask   = src.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(src);
  // A mask that selects every bit is dropped, and the source is then NOT a
  // sub-expression of a `#[..]` postfix — it inherits THIS node's context,
  // so it must not be parenthesised on its own account.
  auto srctxt = [&](bool as_operand) { return src.is_invalid() ? std::string{} : render_value(src, /*operand_ctx=*/as_operand); };
  if (!mask.is_invalid()) {
    if (lnast->get_type(mask) == N::Lnast_ntype_ref) {
      auto rit = range_lohi_.find(std::string(lnast->get_name(mask)));
      if (rit != range_lohi_.end()) {
        // The bounds are TEXT here (a range temp's operands need not be
        // literals); only a numeric pair can be simplified.
        const auto lo = parse_int_const(rit->second.first);
        const auto hi = parse_int_const(rit->second.second);
        if (lo && hi && *lo >= 0 && *hi >= *lo) {
          if (is_whole_width_mask(src, static_cast<int>(*lo), static_cast<int>(*hi))) {
            return srctxt(operand_ctx);  // selects every bit of the source: a no-op
          }
          return fmt_bit_range(srctxt(true), static_cast<int>(*lo), static_cast<int>(*hi));  // tight
        }
        return std::format("{}#[{}..={}]", srctxt(true), rit->second.first, rit->second.second);  // tight
      }
    } else if (lnast->get_type(mask) == N::Lnast_ntype_const) {
      std::string mt(lnast->get_name(mask));
      // An all-ones mask IS `#[..]`, the full bit vector, and that is the only
      // spelling that re-parses for every source kind. `contiguous_run` cannot
      // describe it (there is no highest set bit), so without this the fallback
      // below emits `x & -1` -- which re-parses as an ordinary `&` and is a hard
      // type error the moment the source is a tuple or an array
      // (`operator & requires integer operands (lanes:tuple, …)`), exactly the
      // shape a packed array round-trips as.
      if (mt == "-1") {
        return wrap_operand(std::format("{}#[..]", srctxt(true)), operand_ctx, /*loose=*/false);
      }
      if (auto run = contiguous_run(mt)) {
        if (is_whole_width_mask(src, run->first, run->second)) {
          return srctxt(operand_ctx);  // selects every bit of the source: a no-op
        }
        return fmt_bit_range(srctxt(true), run->first, run->second);  // tight
      }
      return wrap_operand(std::format("{} & {}", srctxt(true), canonical_const_text(mt)), operand_ctx, /*loose=*/true);
    }
  }
  std::string mv = mask.is_invalid() ? std::string("0") : render_value(mask, /*operand_ctx=*/true);
  return wrap_operand(std::format("{} & {}", srctxt(true), mv), operand_ctx, /*loose=*/true);
}

std::string Lnast_prp_writer::render_concat_rhs(Lnast_nid c0, bool operand_ctx) {
  // `concat( dst, v_msb, w_msb, …, v_lsb, w_lsb )` is emitted as the
  // SLICED SHIFT-OR it is equivalent to, NOT as a `concat(...)` call.
  //
  // Pyrope has no syntax for a per-lane window width, and a lane's width is
  // its DECLARED type -- so `concat(a, b)` only re-parses to the same node
  // when every lane already names something declared that wide. The values
  // the writer has here are generated temps and literals, which declare
  // nothing, and the re-parse then fails with `concat-untyped-lane`. (That
  // is exactly what broke every prp-v2prp2v round trip.)
  //
  // Spelling the windows explicitly needs no declarations at all: the slice
  // states the width and the shift states the offset, so the re-parse
  // reconstructs the identical layout. cprop's Or-of-disjoint-SHL
  // canonicalization then folds it straight back into one Concat cell, so
  // the graph this round-trips to is the graph it came from.
  //
  // Why not the nicer `const t0:u4 = …` + `concat(t0, …)` form: that needs
  // a STATEMENT per lane, and render_def_rhs is also called in operand
  // context (a mux arm), where there is nowhere to hoist one to.
  struct W_lane {
    std::string expr;
    int64_t     width  = 0;
    int64_t     offset = 0;
    Lnast_nid   nid;             // the lane's value node (for the width/sign queries below)
    bool        fits_u = false;  // the window is known to hold the value unsigned already
  };
  std::vector<W_lane> wl;
  int64_t             total = 0;
  for (auto v = c0.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(c0); !v.is_invalid();) {
    auto w = lnast->get_sibling_next(v);
    if (w.is_invalid()) {
      break;
    }
    int64_t    bits = 0;
    const auto wtxt = std::string(lnast->get_name(w));
    if (!wtxt.empty() && wtxt != "nil") {
      if (auto d = Dlop::from_pyrope(wtxt); d && d->is_integer() && d->is_just_i64()) {
        bits = d->to_just_i64();
      }
    }
    // An ARRAY lane splices its entries, entry 0 LEAST significant, so it never
    // had a single window of its own -- its width operand is still the `nil`
    // sentinel, and a `bits == 0` lane is DROPPED by the term loop below (the
    // whole splice silently became 0). Spell it as the per-entry reads it
    // means: `arr[k]` re-parses as the same lane list, and each entry's window
    // is the element type the declaration states.
    //
    // The entries go out HIGHEST INDEX FIRST because `wl` is MSB-first while a
    // packed array puts entry 0 at bit 0 (docs/pyrope/10-internals.md, "Bit
    // selection and packing") -- the same flip upass.tolg's lower_concat makes
    // when it emits the array windows. Walking these two loops in opposite
    // directions is what would silently reverse an array in the round trip.
    if (bits == 0 && Lnast_ntype::is_ref(lnast->get_type(v))) {
      const std::string nm(strip_prefix(lnast->get_name(v)));
      auto              sz = array_decl_size_.find(nm);
      auto              el = array_decl_elem_.find(nm);
      // No entry-count cap: the extent came from a declared `[N]`, so the
      // netlist already carries N entries and N terms is proportional to it.
      // A cap here would put the silent `= 0` back for exactly the arrays that
      // are too big to notice it on.
      if (sz != array_decl_size_.end() && el != array_decl_elem_.end() && sz->second > 0) {
        for (int64_t k = sz->second - 1; k >= 0; --k) {
          // An unsigned element already sits inside its own window, so it
          // enters the OR tree as itself; a signed one still needs the slice +
          // `unsigned()` the term loop applies, or its sign would bleed into
          // every entry above it.
          wl.push_back(W_lane{std::format("{}[{}]", nm, k), el->second.bits, 0, Lnast_nid{}, !el->second.is_signed});
        }
        v = lnast->get_sibling_next(w);
        continue;
      }
    }
    wl.push_back(W_lane{render_value(v, /*operand_ctx=*/true), bits, 0, v});
    v = lnast->get_sibling_next(w);
  }
  // MSB-first: a lane sits above every lane after it.
  for (auto it = wl.rbegin(); it != wl.rend(); ++it) {
    it->offset  = total;
    total      += it->width;
  }
  // SystemVerilog replication (`{N{bit}}`) reaches LNAST as N adjacent
  // one-bit lanes that all name the same value. Expanding that shape into
  // N shifts and ORs is exact, but needlessly destroys the compact mux form
  // that cprop and formal engines handle well (Minion has many 66-bit
  // enable masks in its multiply/divide datapath). Preserve the replication
  // as one constant select. This is also safe for an ordinary concat that
  // happens to repeat the same one-bit lane: it denotes the same bitvector.
  //
  // The selector is the lane's BIT 0, not a nonzero test of the whole
  // expression: `l.width == 1` states the WINDOW, and nothing here bounds
  // the lane VALUE to one bit. A repeated 2-bit `2` would pack as all zeros
  // through the slice spelling below and as all ones through a `!= 0` test.
  if (wl.size() > 1 && total > 0 && total <= (1 << 20)
      && std::all_of(wl.begin(), wl.end(), [&](const W_lane& l) { return l.width == 1 && l.expr == wl.front().expr; })) {
    const std::string mask = Dlop::get_mask_value(static_cast<int>(total))->to_pyrope();
    const auto&       sel  = wl.front();
    // A lane already proven to be one unsigned bit IS the selector.
    const std::string test = fits_unsigned_bits(sel.nid, 1) ? sel.expr : std::format("unsigned(({})#[0])", sel.expr);
    return wrap_operand(std::format("if {} != 0 {{ {} }} else {{ 0 }}", test, mask), operand_ctx, /*loose=*/true);
  }
  // `loose` = the text needs parens to sit next to another operator (only a
  // shift does; every other spelling is already atomic or self-parenthesised).
  struct Term {
    std::string text;
    bool        loose = false;
  };
  std::vector<Term> terms;
  for (const auto& l : wl) {
    if (l.width <= 0 || l.width > (1 << 20)) {
      continue;  // unbound (or absurd) width: cannot be spelled; upass reports it
    }
    // The slice is what states the WINDOW: it keeps a wider or negative
    // lane from bleeding into the lane above. Reinterpret that fully-sized
    // slice as unsigned before it enters the OR tree. Without this, a low
    // lane whose top bit is one sign-extends through the whole pack (for
    // example `{33{1'b0}, 32'hffff_fffe}` became 65'h1ffff_ffff_ffff_fffe).
    //
    // Neither is needed for a lane whose value provably already sits in
    // [0, 2^width-1]: nothing can bleed and there is no sign to reinterpret,
    // so the lane enters the OR tree as itself. A ZERO lane then contributes
    // nothing at all and drops out — its window still rides in the widths of
    // the lanes below it, which is what fixes every lane's offset.
    std::string term;
    if (auto k = const_lane_value(l.nid, l.width)) {
      if (*k == "0") {
        continue;
      }
      term = *k;
    } else if (l.fits_u || fits_unsigned_bits(l.nid, l.width)) {
      term = l.expr;
    } else {
      term = std::format("unsigned({})", fmt_bit_range("(" + l.expr + ")", 0, static_cast<int>(l.width) - 1));
    }
    bool loose = false;
    if (l.offset != 0) {
      term  = std::format("{} << {}", term, l.offset);
      loose = true;
    }
    terms.emplace_back(Term{std::move(term), loose});
  }
  if (terms.empty()) {
    return "0";
  }
  if (terms.size() == 1) {  // a lone lane: let the CALLER decide the parens
    return wrap_operand(terms.front().text, operand_ctx, terms.front().loose);
  }
  std::string s;
  for (const auto& term_i : terms) {
    if (!s.empty()) {
      s += " | ";
    }
    // `<<` next to `|` is always parenthesised
    s += term_i.loose ? "(" + term_i.text + ")" : term_i.text;
  }
  return wrap_operand(s, operand_ctx, /*loose=*/true);
}

std::string Lnast_prp_writer::render_tuple_get_rhs(Lnast_nid c0) {
  using N   = Lnast_ntype;
  auto base = lnast->get_sibling_next(c0);
  // A submodule output read `inst["port"]` prints as `inst.port` (dot field
  // access) when `inst` is a module-instance result and `port` is an
  // identifier path. This includes slang's flattened nested output names,
  // e.g. `inst["rsp.header.id"]` -> `inst.rsp.header.id`, and covers both
  // inlined uses and any remaining `wire` driver. Non-instance tuple reads
  // keep the bracket-string form.
  if (!base.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(base))) {
    std::string bn(strip_prefix(lnast->get_name(base)));
    auto        idx0 = lnast->get_sibling_next(base);
    // A module-instance result OR an imported PACKAGE base with a constant
    // identifier path prints as `base.field` (dot access), not
    // `base["field"]` — including provenance `pkg.PARAM` refs that arrive as
    // a tuple_get.
    if (!idx0.is_invalid() && lnast->get_sibling_next(idx0).is_invalid() && lnast->get_type(idx0) == N::Lnast_ntype_const) {
      std::string field(lnast->get_name(idx0));
      if (field.size() >= 2 && (field.front() == '\'' || field.front() == '"') && field.back() == field.front()) {
        field = field.substr(1, field.size() - 2);
      }
      const bool dot_base = instance_results_.count(bn) != 0u || is_imported_package_name(bn) || is_bundle_field(bn + "." + field);
      if (dot_base) {
        if (auto field_path = quote_field_path(field)) {
          return bn + "." + *field_path;  // postfix dot access (binds tight, never wrapped)
        }
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
  if (drops_as_import_residue(nid)) {
    return true;  // an elaborated `const X = import("…")` — the header re-emits it
  }
  if (!dead_init_stmts_.empty() && dead_init_stmts_.count(nid.get_class_index().value) != 0) {
    return true;  // seed overwritten before any read (see scan_dead_init_stores)
  }
  if (t == Lnast_ntype::Lnast_ntype_timecheck) {
    return true;  // inert; dropped (timing already carried by stage[N]/@[N])
  }
  if (t == Lnast_ntype::Lnast_ntype_tuple_add) {
    auto tuple_tmp = lnast->get_child(nid);
    auto next      = lnast->get_sibling_next(nid);
    if (!tuple_tmp.is_invalid() && !next.is_invalid() && lnast->get_type(next) == Lnast_ntype::Lnast_ntype_store) {
      auto type_name = lnast->get_child(next);
      auto stored    = type_name.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(type_name);
      if (!type_name.is_invalid() && !stored.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(stored))
          && lnast->get_name(stored) == lnast->get_name(tuple_tmp)
          && type_declared_.count(std::string(lnast->get_name(type_name))) != 0u) {
        return true;  // structural type constructor, not a runtime tuple value
      }
    }
  }
  if (t == Lnast_ntype::Lnast_ntype_declare && !find_stages_child(nid).is_invalid()) {
    return true;  // stage declare — re-attached to its store as `stage[N] x = v`
  }
  if (lnast->get_lambda_kind() == "pipe" && t == Lnast_ntype::Lnast_ntype_store) {
    auto lhs = lnast->get_child(nid);
    auto rhs = lhs.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(lhs);
    // uPass_pipe materializes the declared pipe latency as
    //   %pipe_o = o; o = %pipe_o
    // around the original body. Re-emitting a `pipe[N]` recreates those flops,
    // so printing the synthetic pair would double the latency.
    if (!lhs.is_invalid() && lnast->get_name(lhs).starts_with("%pipe_")) {
      return true;
    }
    if (!rhs.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(rhs)) && lnast->get_name(rhs).starts_with("%pipe_")) {
      return true;
    }
  }
  return false;
}

// A Pyrope FILE-level unit (`top` with a `stmts` and no `io`) reaches the
// writer AFTER elaboration, so its `const pkg = import("pkg")` lines survive
// only as residue: an `import` func_call into a `%tmp`, a `declare`, and a
// `store` — or, when import_defer already consumed the binding, a bare
// value-less `declare`. Printed literally that reads back as
// ``const t_0 = `import`("'pkg'")`` / `const pkg = 0`, i.e. neither the import
// nor a usable const. Recognise the shape here so write_stmts can drop it and
// the header re-emits the binding from (alias, path).
void Lnast_prp_writer::scan_file_imports() {
  auto      root = lnast->get_root();
  Lnast_nid stmts;
  for (auto c = lnast->get_child(root); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (lnast->get_type(c) == Lnast_ntype::Lnast_ntype_io) {
      stmts = Lnast_nid{};  // an extracted lambda: no file scope, but still scan for residue
      break;
    }
    if (lnast->get_type(c) == Lnast_ntype::Lnast_ntype_stmts) {
      stmts = c;
    }
  }
  file_level_ = !stmts.is_invalid();

  // The binding can sit at file scope OR inside a lambda (func_extract carries a
  // captured `const pkg = import(…)` into the extracted body, where an import
  // does not lower anyway) — so scan the WHOLE tree and hoist every one of them
  // to the file header.
  absl::flat_hash_map<std::string, std::string> tmp_path;  // %tmp -> import path
  for (const auto& s : lnast->depth_preorder()) {
    const auto t = lnast->get_type(s);
    if (t == Lnast_ntype::Lnast_ntype_func_call) {
      auto lhs = lnast->get_child(s);
      if (lhs.is_invalid()) {
        continue;
      }
      auto callee = lnast->get_sibling_next(lhs);
      if (callee.is_invalid() || unquote_callee(lnast->get_name(callee)) != "import") {
        continue;
      }
      auto arg = lnast->get_sibling_next(callee);
      if (arg.is_invalid()) {
        continue;
      }
      import_tmps_.emplace(lnast->get_name(lhs));
      tmp_path.emplace(std::string(lnast->get_name(lhs)), std::string(unquote_callee(lnast->get_name(arg))));
      continue;
    }
    if (t != Lnast_ntype::Lnast_ntype_store) {
      continue;
    }
    auto lhs = lnast->get_child(s);
    if (lhs.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(lhs))) {
      continue;
    }
    auto        rhs = lnast->get_sibling_next(lhs);
    std::string nm(strip_prefix(lnast->get_name(lhs)));
    if (!rhs.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(rhs))) {
      if (auto it = tmp_path.find(std::string(lnast->get_name(rhs))); it != tmp_path.end()) {
        import_bound_.emplace(nm);
        if (std::find_if(file_imports_.begin(), file_imports_.end(), [&](const auto& p) { return p.first == nm; })
            == file_imports_.end()) {
          file_imports_.emplace_back(nm, it->second);
        }
        continue;
      }
    }
    if (file_level_ && lnast->get_parent(s) == stmts) {
      file_stored_.emplace(nm);
    }
  }
}

// True for the statements that make up an elaborated file-scope
// `const X = import("path")` (see scan_file_imports), plus the value-less
// `const` declares constprop left behind for a file-private const it folded
// away — a name with no value, no store and no `pub` export has nothing left to
// emit, and `const X = 0` would be a wrong (and, for an import binding,
// actively misleading) reconstruction.
// `declare( ref, type, const(qualifier), [value] )` — true when the optional
// 4th child (the inline initializer) is present.
bool Lnast_prp_writer::is_declare_with_value(Lnast_nid nid) const {
  auto c = lnast->get_child(nid);
  for (int i = 0; i < 3 && !c.is_invalid(); ++i) {
    c = lnast->get_sibling_next(c);
  }
  return !c.is_invalid();
}

bool Lnast_prp_writer::is_pub_export(std::string_view name) const {
  for (const auto& p : lnast->get_pub_list()) {
    if (p.name == name) {
      return true;
    }
  }
  return false;
}

bool Lnast_prp_writer::drops_as_import_residue(Lnast_nid nid) const {
  const auto t = lnast->get_type(nid);
  if (t == Lnast_ntype::Lnast_ntype_func_call) {
    auto lhs = lnast->get_child(nid);
    return !lhs.is_invalid() && import_tmps_.contains(std::string(lnast->get_name(lhs)));
  }
  if (t == Lnast_ntype::Lnast_ntype_store) {
    auto lhs = lnast->get_child(nid);
    auto rhs = lhs.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(lhs);
    return !rhs.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(rhs))
           && import_tmps_.contains(std::string(lnast->get_name(rhs)));
  }
  if (t != Lnast_ntype::Lnast_ntype_declare) {
    return false;
  }
  auto var = lnast->get_child(nid);
  if (var.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(var))) {
    return false;
  }
  const std::string nm(strip_prefix(lnast->get_name(var)));
  if (import_bound_.contains(nm)) {
    return true;
  }
  if (!file_level_) {
    return false;  // the rest is file-scope elaboration residue only
  }
  auto type_nid = lnast->get_sibling_next(var);
  auto qual     = type_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(type_nid);
  if (qual.is_invalid() || lnast->get_name(qual) != "const") {
    return false;  // `mut`/`reg`/`type`/… keep their declaration
  }
  if (!lnast->get_sibling_next(qual).is_invalid()) {
    return false;  // has an inline value
  }
  if (file_stored_.contains(nm)) {
    return false;  // a later store still needs the declaration
  }
  for (const auto& p : lnast->get_pub_list()) {
    if (p.name == nm) {
      return false;  // an export: write_top re-emits it from the pub values
    }
  }
  return true;
}

std::string Lnast_prp_writer::format_stages(Lnast_nid stages_nid) const {
  auto lo = lnast->get_child(stages_nid);
  if (lo.is_invalid()) {
    return {};
  }
  auto        hi = lnast->get_sibling_next(lo);
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
