//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <ostream>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "lnast.hpp"

// Lnast_prp_writer — emits a Pyrope 3.0 source file from an LNAST.
//
// Standalone walker (does NOT subclass Lnast_writer because Lnast_writer's
// write_* methods are not virtual — the dispatch switch calls them directly
// by name, so subclass overrides are invisible to write_lnast()).
//
// Nodes not yet handled (func_def body, tuple flattening) are emitted with a
// /* TODO: <node-type> */ comment so the output stays parseable.
//
// Usage:
//   auto staging = runner.take_staging();
//   Lnast_prp_writer writer(std::cout, staging);
//   writer.write_all();
class Lnast_prp_writer {
public:
  // Takes lnast by value so the writer owns the tree for its entire lifetime
  // (avoids dangling-reference UB if the caller's shared_ptr is destroyed).
  explicit Lnast_prp_writer(std::ostream& os, std::shared_ptr<Lnast> lnast);
  void write_all();

private:
  std::ostream&          os;
  std::shared_ptr<Lnast> lnast;
  int                    depth{0};

  std::stack<Lnast_nid> nid_stack;
  Lnast_nid             cur;

  // ── Cursor helpers ───────────────────────────────────────────────────────
  bool                         move_to_child();
  bool                         move_to_sibling();
  void                         move_to_parent();
  bool                         is_last_child() const;
  std::string_view             current_text() const;
  Lnast_ntype::Lnast_ntype_int current_ntype() const;

  // ── Output helpers ───────────────────────────────────────────────────────
  void print(std::string_view s);
  void print_indent();
  void println(std::string_view s = "");

  // ── Main dispatch ────────────────────────────────────────────────────────
  void write_node();

  // ── Node writers ─────────────────────────────────────────────────────────
  void write_top();
  void write_module();   // slang-origin: io node + body -> comb|mod NAME(...) -> (...) { … }
  void write_stmts();
  void write_if();
  void write_declare();  // declare(ref, type, qualifier, [value])
  void write_store();    // store(var, level0..levelN, value)
  void write_ref();
  void write_const();
  void write_cassert();
  void write_func_call();
  void write_func_def();
  void write_tuple_add();
  void write_tuple_get();
  void write_attr_set();
  void write_attr_get();
  void write_delay_assign();

  // Infix binary:  LHS = a <op> b [op c …]
  void write_infix(std::string_view op);
  // Prefix unary:  LHS = <op>a
  void write_prefix_unary(std::string_view op);
  // sext — emit as a reparsable `src#sext[0..=pos]` bit-select
  void write_sext();
  // get_mask — emit a `src#[lo..=hi]` bit-select reconstructed from the mask
  void write_get_mask();

  // Serialises a type node (cursor must sit on the type child) into a Pyrope
  // type suffix without moving the cursor: "" for prim_type_none, "bool",
  // "string", or "int"/"uN"/"sN" reconstructed from a prim_type_int range.
  std::string render_type();
  // Same, but on an explicit node id (used by the io-port walk, which navigates
  // the tree directly rather than through the shared cursor). Also handles
  // comp_type_array -> "[N]T".
  std::string render_type_at(Lnast_nid type_nid);

  // Emits the `comb|mod NAME(in:T, …) -> (out:T, …)` header from the io node
  // (cursor-independent; reads the io subtree via direct tree accessors).
  void emit_module_header(Lnast_nid io_nid, bool is_mod);
  // True if the module body declares state (a `reg`/`latch` declare, anywhere
  // in the stmts subtree) — selects `mod` over `comb`.
  bool body_has_state(Lnast_nid stmts_nid) const;
  // The lambda name to emit: the last `.`-component of the top module name
  // (e.g. "trivial_if.fun3" -> "fun3"), so the generated identifier is a plain
  // Pyrope name (no dotted/escaped identifier the re-compile leg would reject).
  std::string lambda_name() const;

  // ── Declaration tracking ─────────────────────────────────────────────────
  // Maps a variable name to its pending storage-class keyword ("mut", "reg",
  // "wire") recorded when write_attr_set() suppresses an attr_set x type kw
  // node.  The NEXT assignment to that variable consumes the keyword (once).
  std::unordered_map<std::string, std::string> pending_decl_;

  // Returns the stored keyword for `lhs` (e.g. "mut") and removes it from
  // the map, or returns "" if no pending declaration exists.
  std::string take_decl_keyword(std::string_view lhs);

  // Names already introduced in the current lambda (io ports, explicit
  // `declare` nodes, and prior first-writes).  Post-upass slang LNAST writes to
  // SSA-renamed user variables (`a___ssa_1`) and bare wires with no `declare`
  // node; Pyrope rejects assignment to an undeclared variable, so the first
  // write to such a name must carry a `mut`.  `___`-prefixed compiler temps
  // auto-declare and are never tracked.
  std::unordered_set<std::string> declared_;

  // Storage-class prefix to print before an assignment LHS: a pending
  // attr_set-type keyword if one is queued, else "mut " on the first write to
  // an untracked non-temp name, else "" (already declared, or a temp).  Marks
  // the name declared as a side effect.
  std::string decl_prefix(std::string_view lhs);

  // Reg/memory flop attributes the slang reader emits as standalone `attr_set`
  // statements (`r.[initial]=N`, `r.[reset_pin]=rst`, `data.[fwd]=0`, …).
  // Pyrope only accepts attribute writes folded into the DECLARATION
  // (`reg r:T:[init=N, reset_pin=rst]`), so write_module pre-collects them here
  // keyed by variable name (assembled "k=v, k=v" body) and write_declare emits
  // the `:[…]` suffix; the standalone attr_set statements are then skipped.
  std::unordered_map<std::string, std::string> folded_attrs_;

  // Walk the top-level body statements and populate folded_attrs_ (mapping the
  // slang attr vocabulary to the Pyrope source one: initial->init,
  // sync->async with the value inverted, everything else verbatim).
  void collect_folded_attrs(Lnast_nid stmts_nid);
  // Render an attr value leaf (const text or ref name) to Pyrope source.
  std::string render_attr_value(Lnast_nid value_nid) const;

  // ── Utilities ────────────────────────────────────────────────────────────
  static bool             is_tmp(std::string_view name);
  static std::string_view strip_prefix(std::string_view name);
};
