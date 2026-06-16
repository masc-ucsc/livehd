//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_prp_writer.hpp"

#include <format>
#include <optional>
#include <string>

// ── Constructor ───────────────────────────────────────────────────────────────

Lnast_prp_writer::Lnast_prp_writer(std::ostream& _os, std::shared_ptr<Lnast> _lnast) : os(_os), lnast(std::move(_lnast)) {}

// ── Public entry point ────────────────────────────────────────────────────────

void Lnast_prp_writer::write_all() {
  depth     = 0;
  nid_stack = {};
  cur       = lnast->get_root();
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

bool Lnast_prp_writer::is_tmp(std::string_view name) {
  return name.size() >= 3 && name[0] == '_' && name[1] == '_' && name[2] == '_';
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
    return {};  // `___` compiler temps auto-declare
  }
  if (declared_.count(std::string(lhs))) {
    return {};
  }
  declared_.insert(std::string(lhs));
  return "mut ";
}

std::string_view Lnast_prp_writer::strip_prefix(std::string_view name) { return name; }

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
    case N::Lnast_ntype_tuple_add   : write_tuple_add(); break;
    case N::Lnast_ntype_tuple_get   : write_tuple_get(); break;
    case N::Lnast_ntype_attr_set    : write_attr_set(); break;
    case N::Lnast_ntype_attr_get    : write_attr_get(); break;
    case N::Lnast_ntype_delay_assign: write_delay_assign(); break;
    case N::Lnast_ntype_plus        : write_infix("+"); break;
    case N::Lnast_ntype_minus       : write_infix("-"); break;
    case N::Lnast_ntype_mult        : write_infix("*"); break;
    case N::Lnast_ntype_div         : write_infix("/"); break;
    case N::Lnast_ntype_mod         : write_infix("%"); break;
    case N::Lnast_ntype_shl         : write_infix("<<"); break;
    case N::Lnast_ntype_sra         : write_infix(">>"); break;
    case N::Lnast_ntype_sext        : write_sext(); break;
    case N::Lnast_ntype_get_mask    : write_get_mask(); break;
    case N::Lnast_ntype_eq          : write_infix("=="); break;
    case N::Lnast_ntype_ne          : write_infix("!="); break;
    case N::Lnast_ntype_lt          : write_infix("<"); break;
    case N::Lnast_ntype_le          : write_infix("<="); break;
    case N::Lnast_ntype_gt          : write_infix(">"); break;
    case N::Lnast_ntype_ge          : write_infix(">="); break;
    case N::Lnast_ntype_log_and     : write_infix("and"); break;
    case N::Lnast_ntype_log_or      : write_infix("or"); break;
    case N::Lnast_ntype_log_not     : write_prefix_unary("not "); break;
    case N::Lnast_ntype_bit_and     : write_infix("&"); break;
    case N::Lnast_ntype_bit_or      : write_infix("|"); break;
    case N::Lnast_ntype_bit_xor     : write_infix("^"); break;
    case N::Lnast_ntype_bit_not     : write_prefix_unary("~"); break;
    default                         : {
      // Unknown node — emit a comment so the output stays parseable.
      println(std::format("/* TODO: unhandled node type {} */", static_cast<int>(current_ntype())));
      break;
    }
  }
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

  const bool is_mod = body_has_state(lnast->get_sibling_next(io_nid));
  // `pub … ::[lg="<name>"]` pins the generated module name to the source module
  // name verbatim, so the re-compile leg and LEC see the SAME identifier as the
  // golden .v.  Without it the lambda name `fun3` would be re-qualified by the
  // .prp filename (`trivial_if.fun3` → `trivial_if.fun3.fun3`, and a non-dotted
  // `chip_top` → `chip_top.chip_top`).  lg requires a `pub` lambda.
  print("pub ");
  print(is_mod ? "mod " : "comb ");
  print(lambda_name());
  print(std::format("::[lg=\"{}\"]", lnast->get_top_module_name()));
  emit_module_header(io_nid, is_mod);
  print(" {\n");
  ++depth;

  auto stmts_nid = lnast->get_sibling_next(io_nid);
  collect_folded_attrs(stmts_nid);  // gather reg/mem attrs to fold into declares
  if (!stmts_nid.is_invalid()) {
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
void Lnast_prp_writer::emit_module_header(Lnast_nid io_nid, bool is_mod) {
  auto emit_group = [&](Lnast_nid tup_nid, bool is_output) {
    print("(");
    bool first = true;
    if (!tup_nid.is_invalid()) {
      for (auto port = lnast->get_child(tup_nid); !port.is_invalid(); port = lnast->get_sibling_next(port)) {
        auto name_nid = lnast->get_child(port);  // ref(name)
        if (name_nid.is_invalid()) {
          continue;
        }
        auto init_nid = lnast->get_sibling_next(name_nid);          // const(init|nil)
        auto type_nid = init_nid.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(init_nid);
        if (!first) {
          print(", ");
        }
        auto pname = strip_prefix(lnast->get_name(name_nid));
        declared_.insert(std::string(pname));  // io ports are pre-declared; body writes skip `mut`
        print(pname);
        if (!type_nid.is_invalid()) {
          auto t = render_type_at(type_nid);
          if (!t.empty()) {
            print(":");
            print(t);
          }
        }
        // Every `mod` output must carry a landing-cycle annotation.  The slang
        // reader leaves the stages node nil/nil (no explicit pipe depth), so
        // opt out of the cycle check with `@[]` — it does not change lowering,
        // only skips the interface-latency assertion.
        if (is_mod && is_output) {
          print("@[]");
        }
        first = false;
      }
    }
    print(")");
  };

  auto in_tup  = lnast->get_child(io_nid);
  auto out_tup = in_tup.is_invalid() ? Lnast_nid{} : lnast->get_sibling_next(in_tup);
  emit_group(in_tup, /*is_output=*/false);
  print(" -> ");
  emit_group(out_tup, /*is_output=*/true);
}

bool Lnast_prp_writer::body_has_state(Lnast_nid nid) const {
  if (nid.is_invalid()) {
    return false;
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
    auto val_nid = lnast->get_sibling_next(key_nid);
    std::string val = render_attr_value(val_nid);

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

  // First child: condition (ref or const)
  print(unique ? "unique if " : "if ");
  write_node();
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
      // elif condition
      print(" elif ");
      write_node();
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
  const bool nil_value
      = has_value && current_ntype() == Lnast_ntype::Lnast_ntype_const && current_text() == "nil";

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
      write_node();
    }
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
      case 1: topbits = 1; break;
      case 3: topbits = 2; break;
      case 7: topbits = 3; break;
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
      case 1: toplog = 0; break;
      case 2: toplog = 1; break;
      case 4: toplog = 2; break;
      case 8: toplog = 3; break;
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
        return "int";  // unbounded integer
      }
      auto c_min = lnast->get_sibling_next(c_max);
      if (c_min.is_invalid()) {
        return "int";  // single-sided bound — no clean uN/sN spelling
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
      return "int";  // safe, lossy fallback — `int` accepts any value and re-parses
    }
    case N::Lnast_ntype_comp_type_array: {
      // comp_type_array( elem_type, const("[N]") ) -> "[N]elemtype".  The size
      // const already carries its brackets (e.g. "[4]"), so concatenate as-is.
      auto elem = lnast->get_child(type_nid);
      if (elem.is_invalid()) {
        return {};
      }
      auto size_n = lnast->get_sibling_next(elem);
      std::string sz = size_n.is_invalid() ? std::string{} : std::string(lnast->get_name(size_n));
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
  auto lhs = strip_prefix(current_text());
  print(decl_prefix(lhs));
  print(lhs);
  while (move_to_sibling() && !is_last_child()) {
    print("[");
    write_node();
    print("]");
  }
  print(" = ");
  write_node();
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
  write_node();
  // Optional 2nd child: a comptime message string (cassert(cond, "msg")).
  if (move_to_sibling()) {
    print(", ");
    write_node();
  }
  print(")");
  move_to_parent();
}

// ── func_call ─────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_func_call() {
  if (!move_to_child()) {
    return;
  }
  // LHS
  auto lhs = strip_prefix(current_text());
  print(decl_prefix(lhs));
  print(lhs);
  print(" = ");
  // function name
  move_to_sibling();
  print(current_text());
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
          write_node();  // argument value
        }
        move_to_parent();
      }
    } else {
      write_node();
    }
    first = false;
  }
  print(")");
  move_to_parent();
}

// ── func_def ──────────────────────────────────────────────────────────────────

void Lnast_prp_writer::write_func_def() {
  // Slice 4 not yet implemented — emit a TODO comment block.
  print("/* TODO: func_def — Slice 4 not yet implemented */");
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
    write_node();
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
      write_node();
      first = false;
    } while (move_to_sibling());
    move_to_parent();
  }
  print(")");
}

void Lnast_prp_writer::write_tuple_get() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  print(decl_prefix(lhs));
  print(lhs);
  print(" = ");
  move_to_sibling();
  print(strip_prefix(current_text()));
  while (move_to_sibling()) {
    print("[");
    write_node();
    print("]");
  }
  move_to_parent();
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
    write_node();  // value
  }

  move_to_parent();
}

void Lnast_prp_writer::write_attr_get() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  print(decl_prefix(lhs));
  print(lhs);
  print(" = ");
  move_to_sibling();
  print(strip_prefix(current_text()));
  while (move_to_sibling()) {
    print(".");
    write_node();
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
  write_node();
  if (move_to_sibling()) {
    print(", ");
    write_node();
  }
  print("]");
  move_to_parent();
}

// ── Infix / prefix helpers ────────────────────────────────────────────────────

void Lnast_prp_writer::write_infix(std::string_view op) {
  if (!move_to_child()) {
    return;
  }

  // First child is the LHS (result variable).
  auto lhs = strip_prefix(current_text());
  print(decl_prefix(lhs));
  print(lhs);
  print(" = ");

  // Remaining siblings are the RHS operands.  Serialize each through
  // write_node() so nested non-leaf nodes are handled correctly.
  bool first = true;
  while (move_to_sibling()) {
    if (!first) {
      print(" ");
      print(op);
      print(" ");
    }
    write_node();
    first = false;
  }
  move_to_parent();
}

void Lnast_prp_writer::write_prefix_unary(std::string_view op) {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  print(decl_prefix(lhs));
  print(lhs);
  print(" = ");
  print(op);
  if (move_to_sibling()) {
    write_node();
  }
  move_to_parent();
}

// sext( dst, src, pos ) — sign-extend `src` treating bit `pos` as the sign bit.
// Reparsable spelling: `src#sext[0..=pos]`.  prp2lnast lowers `#sext[0..=pos]`
// to get_mask(src, (1<<(pos+1))-1) then sext(_, pos); when `src` is already
// exactly pos+1 bits wide (the only shape the corpus produces — the sext source
// is the prior get_mask slice), the inner get_mask is the identity, so the
// round-trip reproduces the same sext.
void Lnast_prp_writer::write_sext() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  print(decl_prefix(lhs));
  print(lhs);
  print(" = ");
  move_to_sibling();
  auto src = std::string(strip_prefix(current_text()));
  auto pos = std::string{"0"};
  if (move_to_sibling()) {
    pos = std::string(strip_prefix(current_text()));
  }
  move_to_parent();
  os << std::format("{}#sext[0..={}]", src, pos);
}

// get_mask( dst, src, mask ) — extract the bits of `src` selected by `mask`,
// packed LSB-first.  For a contiguous run of set bits [lo..hi] this is exactly
// `src#[lo..=hi]`; that is all the corpus produces (width-truncation masks
// (1<<n)-1, plus a couple of non-zero-based contiguous slices).  Non-contiguous
// masks fall back to `src & mask` (numerically equal only when the mask is
// zero-based, but keeps the output reparsable rather than emitting a comment).
void Lnast_prp_writer::write_get_mask() {
  if (!move_to_child()) {
    return;
  }
  auto lhs = strip_prefix(current_text());
  print(decl_prefix(lhs));
  print(lhs);
  print(" = ");

  move_to_sibling();  // src
  auto src = std::string(strip_prefix(current_text()));

  std::string mask_txt;
  if (move_to_sibling()) {  // mask const
    mask_txt = std::string(current_text());
  }
  move_to_parent();

  // A contiguous low mask 2^N-1 (the width-truncation case, incl. wide >64-bit
  // data buses) is `src#[0..=N-1]`.
  if (int n = all_ones_width(mask_txt); n > 0) {
    os << std::format("{}#[0..={}]", src, n - 1);
    return;
  }
  // A contiguous run not based at bit 0 (narrow): `src#[lo..=hi]`.
  auto maskv = parse_int_const(mask_txt);
  if (maskv && *maskv > 0) {
    unsigned long long m = static_cast<unsigned long long>(*maskv);
    int lo = 0;
    while (((m >> lo) & 1ULL) == 0ULL) {
      ++lo;
    }
    int hi = 63;
    while (hi > lo && ((m >> hi) & 1ULL) == 0ULL) {
      --hi;
    }
    unsigned long long contiguous = ((hi - lo) >= 63) ? ~0ULL : (((1ULL << (hi - lo + 1)) - 1ULL) << lo);
    if (contiguous == m) {
      os << std::format("{}#[{}..={}]", src, lo, hi);
      return;
    }
  }
  // Non-contiguous / unparsable mask: fall back to a plain bitwise AND.
  os << std::format("{} & {}", src, mask_txt);
}
