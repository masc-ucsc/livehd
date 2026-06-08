//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <ostream>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>

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
  void write_stmts();
  void write_if();
  void write_declare();  // task 1t — declare(ref, type, qualifier, [value])
  void write_store();    // task 1t — store(var, level0..levelN, value)
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
  // sext — no Pyrope operator; emit as call with comment
  void write_sext();

  // Serialises a type node (cursor must sit on the type child) into a Pyrope
  // type suffix without moving the cursor: "" for prim_type_none, "bool",
  // "string", or "int"/"uN"/"sN" reconstructed from a prim_type_int range.
  std::string render_type();

  // ── Declaration tracking ─────────────────────────────────────────────────
  // Maps a variable name to its pending storage-class keyword ("mut", "reg",
  // "wire") recorded when write_attr_set() suppresses an attr_set x type kw
  // node.  The NEXT assignment to that variable consumes the keyword (once).
  std::unordered_map<std::string, std::string> pending_decl_;

  // Returns the stored keyword for `lhs` (e.g. "mut") and removes it from
  // the map, or returns "" if no pending declaration exists.
  std::string take_decl_keyword(std::string_view lhs);

  // ── Utilities ────────────────────────────────────────────────────────────
  static bool             is_tmp(std::string_view name);
  static std::string_view strip_prefix(std::string_view name);
};
