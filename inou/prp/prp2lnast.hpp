//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <functional>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "lnast.hpp"
#include "lnast_builder.hpp"
#include "lnast_ntype.hpp"
#include "symbol_table.hpp"
#include "tree_sitter/api.h"

class Prp2lnast {
protected:
  // TS Parsing
  std::string prp_file;
  TSParser*   parser;
  TSNode      ts_root_node;

  // LNAST output. `builder` co-owns `lnast` and is the canonical home for
  // the current `idx_stmts` cursor, tmp-ref minting, and frontend-agnostic
  // stmt emitters (cleanup_todo §3.4).
  std::shared_ptr<Lnast> lnast;
  Lnast_builder          builder;

  // Pending overflow kind ("wrap"/"sat") to apply to the next assignment.
  // Set by process_{description,scope_statement} when the new grammar's
  // statement-level `wrap`/`sat` prefix is seen; consumed by process_assignment.
  // ts_node_type strings have static lifetime, so a string_view is safe.
  std::string_view pending_overflow_kind;

  // Tree-sitter currently doesn't always attach a `comb foo(...) { ... }`
  // body to the lambda's `code` field; for some inputs the body parses as
  // a separate sibling scope_statement next to the lambda. process_lambda
  // detects that pattern, uses the sibling as the body, and records its
  // start byte here so the enclosing walker (process_description /
  // process_scope_statement) skips it on the next iteration. Without
  // this, the body content would also emit as an orphan top-level stmts.
  std::unordered_set<uint32_t> consumed_lambda_body_starts;

  // Comptime-known tuples used by process_for_statement to unroll
  //   `for (e[, idx[, key]]) in NAME { … }` over a static shape, and to size
  //   `for i in ref NAME { … }` ref-iterations (write-back unroll).
  //
  // Recording paths:
  //   - `const NAME = <literal_tuple>` with all-literal entries → entries
  //     have `value_known=true` and `value_text` set; powers value-bound
  //     unrolls (tuple_exclude).
  //   - `mut NAME = ()` and `mut NAME = <literal_tuple>` → tracks the size,
  //     mutating ops below keep size in sync.
  //   - `NAME = NAME ++ <literal_tuple>` (or `NAME ++= <literal_tuple>`)
  //     where the rhs is a positional/named tuple → appends placeholder
  //     entries with `value_known=false` (size grows but the per-iter values
  //     aren't statically known).
  //   - Any other write to NAME invalidates by erasing the entry.
  //
  // Empty `key` means a positional slot. `value_text` is only consumed when
  // `value_known` is true.
  struct Comptime_tuple_entry {
    std::string key;         // empty string for positional entries
    std::string value_text;  // pyrope constant literal (verbatim source text)
    bool        value_known = false;
  };
  std::unordered_map<std::string, std::vector<Comptime_tuple_entry>> comptime_tuples_;

  // Top
  void process_description();

  // Statements
  void process_statement(TSNode n);
  void process_scope_statement(TSNode n, Lnast_nid target_stmts);
  // Shared body for process_description / process_scope_statement: walks ALL
  // children of `parent` (named + anonymous) so the grammar's hidden `wrap`/
  // `sat` overflow tokens and `gate` field bubble-ups are visible.
  void walk_statement_block(TSNode parent);
  // Wrap a statement in a synthesized `if cond { stmt }` (or `if !cond { stmt }`
  // for `unless`). For decl-form assignments (`mut x = e when cond`) the decl
  // is hoisted to the surrounding scope with a `nil` initializer, so `x` is
  // visible regardless of the gate result.
  void process_gated_statement(TSNode stmt, TSNode gate);
  void process_assignment(TSNode n);
  void process_declaration_statement(TSNode n);
  void process_assert_statement(TSNode n);
  void process_while_statement(TSNode n);
  void process_for_statement(TSNode n);
  void process_loop_statement(TSNode n);
  void process_control_statement(TSNode n);
  void process_function_call_statement(TSNode n);
  void process_lambda_statement(TSNode n);
  void process_enum_assignment(TSNode n);
  void process_type_statement(TSNode n);
  void process_import_statement(TSNode n);
  void process_test_statement(TSNode n);
  void process_spawn_statement(TSNode n);
  void process_impl_statement(TSNode n);

  // Expressions: returns an Lnast_node (ref or const) naming the result
  Lnast_node expr_to_node(TSNode n);
  Lnast_node binary_expr_to_node(TSNode n);
  Lnast_node unary_expr_to_node(TSNode n);
  Lnast_node if_expr_to_node(TSNode n, bool need_result = true);
  Lnast_node match_expr_to_node(TSNode n, bool need_result = true);
  Lnast_node bit_selection_to_node(TSNode n);
  Lnast_node member_selection_to_node(TSNode n);
  Lnast_node attribute_read_to_node(TSNode n);
  Lnast_node dot_expression_to_node(TSNode n);
  Lnast_node function_call_expr_to_node(TSNode n);
  Lnast_node interpolated_string_to_node(TSNode n);
  Lnast_node tuple_to_node(TSNode n, bool is_square);
  Lnast_node identifier_to_node(TSNode n, bool for_lvalue);
  Lnast_node constant_text_to_node(std::string_view text);
  Lnast_node type_specification_to_node(TSNode n);

  // Type handling
  void emit_type_spec(const Lnast_node& target, TSNode type_cast_node);
  void emit_attribute_list(const Lnast_node& target, TSNode attribute_list_node);
  void emit_type_expr(const Lnast_nid& type_index, TSNode type_node);

  struct Call_arg {
    bool        is_assign = false;
    std::string assign_key;
    Lnast_node  value;
  };
  std::vector<Call_arg> collect_call_args(TSNode arg_tuple);
  void                  add_call_args_to_fcall(const Lnast_nid& fcall_idx, const std::vector<Call_arg>& call_args);

  // Lvalue helpers
  Lnast_node process_lvalue_for_assign(TSNode lvalue, const Lnast_node& rvalue, TSNode decl_node, TSNode type_cast_node);

  // Helpers
  std::string_view        get_text(const TSNode& n) const;
  std::string             get_text_str(const TSNode& n) const { return std::string(get_text(n)); }
  static std::string_view trim(std::string_view s);
  std::string_view        text_between(uint32_t start, uint32_t end) const;

  // Get rvalue text even when the rvalue field is a hidden token (numbers,
  // bool/string literals, '?'). Returns the text between the '=' operator's
  // end and the end of the enclosing parent span.
  std::string rvalue_text_fallback(TSNode parent, TSNode after_field) const;
  // Get binary_expression right operand text when hidden.
  std::string binary_right_text(TSNode bin, TSNode operator_node) const;
  // Get binary_expression left operand text when hidden.
  std::string binary_left_text(TSNode bin, TSNode operator_node) const;

  // TS API helpers
  inline TSNode   child_by_field(const TSNode& n, const char* field) const;
  inline uint32_t child_count(const TSNode& n) const { return ts_node_child_count(n); }
  inline TSNode   child(const TSNode& n, uint32_t i) const { return ts_node_child(n, i); }
  inline TSNode   named_child(const TSNode& n, uint32_t i) const { return ts_node_named_child(n, i); }
  inline uint32_t named_child_count(const TSNode& n) const { return ts_node_named_child_count(n); }

public:
  Prp2lnast(std::string_view filename, std::string_view module_name, bool parse_only);

  ~Prp2lnast();

  std::shared_ptr<Lnast> get_lnast() { return std::move(lnast); }
  void                   dump_tree_sitter() const;
  void                   dump_tree_sitter(TSTreeCursor* tc, int level) const;
  void                   dump() const;
};
