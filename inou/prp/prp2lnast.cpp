//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp2lnast.hpp"

#include <cctype>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "pass.hpp"
#include "str_tools.hpp"

extern "C" TSLanguage* tree_sitter_pyrope();

Prp2lnast::Prp2lnast(std::string_view filename, std::string_view module_name, bool parse_only = false) {
  lnast = std::make_shared<Lnast>(module_name);

  lnast->set_root(Lnast_ntype::create_top());

  // Builder co-owns lnast so its tmp counter and (future) cursor track the
  // same tree Prp2lnast is mutating directly.
  builder.lnast = lnast;

  {
    std::string   fname(filename);
    auto          ss = std::ostringstream{};
    std::ifstream file(fname);
    ss << file.rdbuf();
    prp_file = ss.str();
  }

  parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_pyrope());

  TSTree* tst_tree = ts_parser_parse_string(parser, NULL, prp_file.data(), prp_file.size());
  ts_root_node     = ts_tree_root_node(tst_tree);

  // dump();  // tree-sitter parse tree; enable for debugging the grammar

  if (parse_only) {
    return;
  }

  process_description();
}

Prp2lnast::~Prp2lnast() { ts_parser_delete(parser); }

std::string_view Prp2lnast::get_text(const TSNode& node) const {
  auto start  = ts_node_start_byte(node);
  auto end    = ts_node_end_byte(node);
  auto length = end - start;

  I(end <= prp_file.size());
  return std::string_view(prp_file).substr(start, length);
}

std::string_view Prp2lnast::text_between(uint32_t start, uint32_t end) const {
  if (start > end) {
    return {};
  }
  I(end <= prp_file.size());
  return std::string_view(prp_file).substr(start, end - start);
}

std::string_view Prp2lnast::trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return s;
}

void Prp2lnast::dump_tree_sitter() const {
  auto tc = ts_tree_cursor_new(ts_root_node);

  dump_tree_sitter(&tc, 1);

  ts_tree_cursor_delete(&tc);
}

void Prp2lnast::dump_tree_sitter(TSTreeCursor* tc, int level) const {
  auto indent = std::string(level * 2, ' ');

  bool go_next = true;
  while (go_next) {
    auto             node         = ts_tree_cursor_current_node(tc);
    auto             num_children = ts_node_child_count(node);
    std::string_view node_type(ts_node_type(node));

    std::print("{}{} {}\n", indent, node_type, num_children);

    if (num_children) {
      ts_tree_cursor_goto_first_child(tc);
      dump_tree_sitter(tc, level + 1);
      ts_tree_cursor_goto_parent(tc);
    }

    go_next = ts_tree_cursor_goto_next_sibling(tc);
  }
}

void Prp2lnast::dump() const {
  std::cout << "tree-sitter-dump\n";

  dump_tree_sitter();
}

inline TSNode Prp2lnast::child_by_field(const TSNode& node, const char* field) const {
  return ts_node_child_by_field_name(node, field, std::char_traits<char>::length(field));
}

// ---------------- Top level ----------------

void Prp2lnast::process_description() {
  builder.idx_stmts = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
  walk_statement_block(ts_root_node);
}

void Prp2lnast::walk_statement_block(TSNode parent) {
  // Walk ALL children (not just named) so the anonymous `wrap`/`sat` token
  // that the new grammar attaches as the `overflow` field on an assignment
  // statement can be detected — it precedes the assignment as a sibling
  // because `_statement` is hidden. Similarly, a `when_unless_cond` node
  // that the grammar attaches as the `gate` field of the hidden `_semicolon`
  // bubbles up as a sibling immediately after the gated statement.
  using namespace std::string_view_literals;
  const uint32_t   nc = ts_node_child_count(parent);
  std::string_view pending_overflow;
  for (uint32_t i = 0; i < nc; i++) {
    TSNode           c = ts_node_child(parent, i);
    std::string_view t(ts_node_type(c));

    // `gate` siblings are consumed by the previous statement via lookahead.
    const char* fname = ts_node_field_name_for_child(parent, i);
    if (fname && std::string_view(fname) == "gate"sv) {
      continue;
    }

    if (!ts_node_is_named(c)) {
      if (t == "wrap"sv || t == "sat"sv) {
        pending_overflow = t;
      }
      continue;
    }
    // A scope_statement that was already consumed as a lambda body must
    // not be re-walked here, otherwise the body content emits twice
    // (once inside the lambda's body_idx, once as an orphan top-level
    // stmts). See `consumed_lambda_body_starts`.
    if (consumed_lambda_body_starts.contains(ts_node_start_byte(c))) {
      pending_overflow = {};
      continue;
    }

    // Look ahead for a sibling whose field name is `gate` — it carries the
    // when/unless condition that gates this statement.
    TSNode gate{};
    bool   have_gate = false;
    for (uint32_t j = i + 1; j < nc; j++) {
      TSNode      g  = ts_node_child(parent, j);
      const char* gf = ts_node_field_name_for_child(parent, j);
      if (gf && std::string_view(gf) == "gate"sv) {
        gate      = g;
        have_gate = true;
        break;
      }
      if (ts_node_is_named(g)) {
        break;
      }
    }

    const bool is_assign = (t == "assignment"sv);
    if (is_assign && !pending_overflow.empty()) {
      pending_overflow_kind = pending_overflow;
    }
    if (have_gate) {
      process_gated_statement(c, gate);
    } else {
      process_statement(c);
    }
    pending_overflow_kind = {};
    pending_overflow      = {};
  }
}

void Prp2lnast::process_statement(TSNode n) {
  if (ts_node_is_null(n)) {
    return;
  }
  using Handler                                                             = void (Prp2lnast::*)(TSNode);
  static const absl::flat_hash_map<std::string_view, Handler> stmt_dispatch = {
      {"declaration_statement", &Prp2lnast::process_declaration_statement},
      {"assignment", &Prp2lnast::process_assignment},
      {"assert_statement", &Prp2lnast::process_assert_statement},
      {"while_statement", &Prp2lnast::process_while_statement},
      {"for_statement", &Prp2lnast::process_for_statement},
      {"loop_statement", &Prp2lnast::process_loop_statement},
      {"control_statement", &Prp2lnast::process_control_statement},
      {"function_call_statement", &Prp2lnast::process_function_call_statement},
      {"lambda", &Prp2lnast::process_lambda_statement},
      {"enum_assignment", &Prp2lnast::process_enum_assignment},
      {"type_statement", &Prp2lnast::process_type_statement},
      {"import_statement", &Prp2lnast::process_import_statement},
      {"test_statement", &Prp2lnast::process_test_statement},
      {"spawn_statement", &Prp2lnast::process_spawn_statement},
      {"impl_statement", &Prp2lnast::process_impl_statement},
  };
  // Expression-as-statement node kinds (lowered for side effects).
  static const absl::flat_hash_set<std::string_view> expr_stmt = {
      "if_expression",
      "match_expression",
      "expression_item",
      "unary_expression",
      "bit_selection",
      "member_selection",
      "attribute_read",
      "dot_expression",
      "function_call_expression",
      "identifier",
      "tuple",
      "tuple_sq",
      "type_specification",
      "constant",
  };

  std::string_view t(ts_node_type(n));
  if (t == "comment") {
    return;
  }
  if (t == "scope_statement") {
    auto inner_stmts = builder.add_child(Lnast_ntype::create_stmts());
    builder.push_stmts(inner_stmts);
    process_scope_statement(n, inner_stmts);
    builder.pop_stmts();
    return;
  }
  if (auto it = stmt_dispatch.find(t); it != stmt_dispatch.end()) {
    (this->*it->second)(n);
    return;
  }
  if (expr_stmt.contains(t)) {
    (void)expr_to_node(n);
    return;
  }
  std::print("prp2lnast: unhandled statement type `{}`\n", t);
}

void Prp2lnast::process_scope_statement(TSNode n, Lnast_nid /*target_stmts*/) { walk_statement_block(n); }

void Prp2lnast::process_gated_statement(TSNode stmt, TSNode gate) {
  // gate is a `when_unless_cond` node:
  //   when_unless_cond := ('when' | 'unless') condition:_expression
  //
  // Lower `stmt when c`   to `if c { stmt }`
  //       `stmt unless c` to `if !c { stmt }`
  //
  // For decl-form assignments (`mut x = e when c`) the binding semantics
  // differ from a plain `if`: the variable must be visible in the enclosing
  // scope regardless of the gate (with value `nil` when the gate is false).
  // We hoist the decl out of the synthesized `if` and seed `nil`, so the
  // gate's body becomes a bare assignment to the already-declared name.
  // TODO(grammar_todo2.md #1): once the grammar splits when/unless into
  // distinct named nodes, dispatch on node kind here.
  std::string_view gate_kw_text = trim(get_text(gate));
  bool             is_unless    = gate_kw_text.starts_with("unless");

  TSNode cond_node = child_by_field(gate, "condition");

  std::string_view stmt_type(ts_node_type(stmt));
  bool             has_decl_assign = false;
  if (stmt_type == "assignment") {
    TSNode decl     = child_by_field(stmt, "decl");
    has_decl_assign = !ts_node_is_null(decl);
  }

  // Hoist the decl + nil-init for `<decl> x = e when c`. After this the
  // variable exists in the surrounding scope; the synthesized if body just
  // overwrites it on the gate-true path.
  if (has_decl_assign) {
    TSNode lv   = child_by_field(stmt, "lvalue");
    TSNode decl = child_by_field(stmt, "decl");
    TSNode tc   = child_by_field(stmt, "type");
    (void)process_lvalue_for_assign(lv, Lnast_node::create_const("nil"), decl, tc);
  }

  // Evaluate the gate condition into the surrounding scope so any
  // helper stmts emitted by expr_to_node land before the `if`.
  Lnast_node cref;
  if (ts_node_is_null(cond_node)) {
    cref = Lnast_node::create_const("true");
  } else {
    cref = expr_to_node(cond_node);
  }
  if (is_unless) {
    auto idx     = builder.add_child(Lnast_ntype::create_log_not());
    auto neg_ref = builder.mint_tmp_ref();
    lnast->add_child(idx, neg_ref);
    lnast->add_child(idx, cref);
    cref = neg_ref;
  }

  auto if_idx = builder.add_child(Lnast_ntype::create_if());
  lnast->add_child(if_idx, cref);
  auto body_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);

  if (has_decl_assign) {
    // Re-emit the assignment without the decl: the binding was already
    // hoisted above. We mirror process_assignment's rvalue-fallback path
    // for hidden constant rvalues.
    TSNode lv = child_by_field(stmt, "lvalue");
    TSNode op = child_by_field(stmt, "operator");
    TSNode rv = child_by_field(stmt, "rvalue");
    TSNode tc = child_by_field(stmt, "type");

    Lnast_node rvalue_node;
    if (ts_node_is_null(rv)) {
      auto op_end  = ts_node_end_byte(op);
      auto par_end = ts_node_end_byte(stmt);
      auto text    = trim(text_between(op_end, par_end));
      rvalue_node  = constant_text_to_node(text);
    } else {
      rvalue_node = expr_to_node(rv);
    }
    TSNode null_decl{};
    (void)process_lvalue_for_assign(lv, rvalue_node, null_decl, tc);
  } else {
    process_statement(stmt);
  }

  builder.pop_stmts();
}

// ---------------- Declaration / Assignment ----------------

// Map a `var_or_let_or_reg` storage child's node kind to the storage-class
// keyword used downstream (attr_set "type" const|mut|reg|await).
static std::string_view storage_kind_from_node_type(std::string_view t) {
  if (t == "const_decl") {
    return "const";
  }
  if (t == "mut_decl") {
    return "mut";
  }
  if (t == "reg_decl") {
    return "reg";
  }
  if (t == "await_decl") {
    return "await";
  }
  return {};
}

// Resolve (storage-kind, has-comptime) for a `var_or_let_or_reg` decl node.
static std::pair<std::string_view, bool> decode_decl(TSNode decl) {
  if (ts_node_is_null(decl)) {
    return {{}, false};
  }
  TSNode           storage = ts_node_child_by_field_name(decl, "storage", 7);
  TSNode           cpt     = ts_node_child_by_field_name(decl, "comptime", 8);
  std::string_view kind    = ts_node_is_null(storage) ? std::string_view{} : storage_kind_from_node_type(ts_node_type(storage));
  return {kind, !ts_node_is_null(cpt)};
}

void Prp2lnast::process_declaration_statement(TSNode n) {
  // declaration_statement: var_or_let_or_reg + lvalue (typed_identifier or list)
  auto decl_node   = child_by_field(n, "decl");
  auto lvalue_node = child_by_field(n, "lvalue");
  if (ts_node_is_null(lvalue_node)) {
    return;
  }

  // Decode storage class + comptime modifier from the decl's structured fields.
  auto [kind, has_comptime] = decode_decl(decl_node);

  // For each typed_identifier in the lvalue, emit: attr_set <ref> "type" kind
  auto emit_decl_attrs = [&](TSNode ti) {
    TSNode id = child_by_field(ti, "identifier");
    if (ts_node_is_null(id)) {
      return;
    }
    Lnast_node ref = identifier_to_node(id, /*for_lvalue=*/true);
    {
      auto idx = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("type"));
      lnast->add_child(idx, Lnast_node::create_const(kind));
    }
    if (has_comptime) {
      auto idx = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("comptime"));
      lnast->add_child(idx, Lnast_node::create_const("true"));
    }
    TSNode tc = child_by_field(ti, "type");
    if (!ts_node_is_null(tc)) {
      emit_type_spec(ref, tc);
    }
  };

  std::string_view lvtype(ts_node_type(lvalue_node));
  if (lvtype == "typed_identifier") {
    emit_decl_attrs(lvalue_node);
  } else if (lvtype == "typed_identifier_list") {
    uint32_t nc = ts_node_named_child_count(lvalue_node);
    for (uint32_t i = 0; i < nc; i++) {
      emit_decl_attrs(ts_node_named_child(lvalue_node, i));
    }
  }
}

Lnast_node Prp2lnast::process_lvalue_for_assign(TSNode lvalue, const Lnast_node& rvalue, TSNode decl_node, TSNode type_cast_node) {
  std::string_view lvt(ts_node_type(lvalue));
  if (lvt == "lvalue_list") {
    // Tuple lvalue: `(x0, x1, …) = rhs`. The grammar's `lvalue_list`
    // children are `lvalue_item`s, each either a `typed_identifier` directly
    // or a `(_complex_identifier, optional type_cast)` pair. For each item
    // we extract the matching positional field from `rvalue` into a fresh
    // tmp and recurse. The recursion lets nested cases (member_selection,
    // dot_expression, even nested lvalue_lists) reuse the existing single-
    // lvalue paths below.
    uint32_t nnc = ts_node_named_child_count(lvalue);
    uint32_t pos = 0;
    for (uint32_t i = 0; i < nnc; i++) {
      TSNode           item = ts_node_named_child(lvalue, i);
      std::string_view itt(ts_node_type(item));
      if (itt != "lvalue_item") {
        continue;
      }
      // Resolve the inner lvalue + per-item type_cast.
      TSNode inner;
      TSNode item_tc      = type_cast_node;  // default: outer type_cast (lvalue_list has none today)
      TSNode item_id_only = child_by_field(item, "identifier");
      TSNode item_tc_only = child_by_field(item, "type");
      if (!ts_node_is_null(item_id_only)) {
        // Option 2: bare _complex_identifier with optional type_cast.
        inner = item_id_only;
        if (!ts_node_is_null(item_tc_only)) {
          item_tc = item_tc_only;
        }
      } else {
        // Option 1: lvalue_item wraps a typed_identifier (or other lvalue
        // node) as its sole named child.
        inner = ts_node_named_child(item, 0);
      }
      if (ts_node_is_null(inner)) {
        ++pos;
        continue;
      }
      // tuple_get tmp, rvalue, <pos>
      auto       tg_idx = builder.add_child(Lnast_ntype::create_tuple_get());
      Lnast_node tmp    = builder.mint_tmp_ref();
      lnast->add_child(tg_idx, tmp);
      lnast->add_child(tg_idx, rvalue);
      lnast->add_child(tg_idx, Lnast_node::create_const(std::to_string(pos)));
      // Recurse: the per-position tmp is the new "rvalue" for the inner lvalue.
      // `decl_node` propagates so `mut (a, b) = …` declares both items.
      (void)process_lvalue_for_assign(inner, tmp, decl_node, item_tc);
      ++pos;
    }
    return rvalue;
  }
  if (lvt == "identifier" || lvt == "typed_identifier") {
    TSNode id = (lvt == "typed_identifier") ? child_by_field(lvalue, "identifier") : lvalue;
    TSNode tc = (lvt == "typed_identifier") ? child_by_field(lvalue, "type") : type_cast_node;
    if (ts_node_is_null(id)) {
      id = lvalue;
    }
    // Determine if this introduces a new declaration (decl_node present)
    bool             has_decl = !ts_node_is_null(decl_node);
    std::string_view kind_sv;
    bool             has_cpt = false;
    if (has_decl) {
      auto pr = decode_decl(decl_node);
      kind_sv = pr.first;
      has_cpt = pr.second;
    }

    Lnast_node ref = identifier_to_node(id, /*for_lvalue=*/true);
    if (has_decl) {
      auto idx = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("type"));
      lnast->add_child(idx, Lnast_node::create_const(kind_sv));
    }
    if (has_cpt) {
      auto idx = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("comptime"));
      lnast->add_child(idx, Lnast_node::create_const("true"));
    }
    if (!ts_node_is_null(tc)) {
      emit_type_spec(ref, tc);
    }

    // Emit assign
    auto aidx = builder.add_child(Lnast_ntype::create_assign());
    lnast->add_child(aidx, ref);
    lnast->add_child(aidx, rvalue);
    return ref;
  }
  if (lvt == "member_selection" || lvt == "dot_expression") {
    // Extract a deep path so `t.b[0] = …` lowers to `tuple_set t b 0 …`
    // (one tuple_set rooted on the actual variable) instead of going
    // through `tuple_get tmp t b; tuple_set tmp 0 …` — the tmp would not
    // propagate the update back to t. The walker handles arbitrarily
    // nested member_selection / dot_expression chains.
    std::vector<Lnast_node>     path;
    Lnast_node                  root;
    bool                        have_root = false;
    std::function<void(TSNode)> collect   = [&](TSNode n) {
      std::string_view t(ts_node_type(n));
      if (t == "dot_expression") {
        TSNode item = child_by_field(n, "item");
        if (ts_node_is_null(item)) {
          uint32_t nc = ts_node_named_child_count(n);
          if (nc > 0) {
            collect(ts_node_named_child(n, 0));
          }
          for (uint32_t i = 1; i < nc; i++) {
            TSNode           c = ts_node_named_child(n, i);
            std::string_view ct(ts_node_type(c));
            if (ct == "identifier") {
              path.push_back(Lnast_node::create_const(trim(get_text(c))));
            } else {
              path.push_back(expr_to_node(c));
            }
          }
        } else {
          collect(item);
          uint32_t nc = ts_node_named_child_count(n);
          for (uint32_t i = 0; i < nc; i++) {
            TSNode c = ts_node_named_child(n, i);
            if (ts_node_eq(c, item)) {
              continue;
            }
            std::string_view ct(ts_node_type(c));
            if (ct == "identifier") {
              path.push_back(Lnast_node::create_const(trim(get_text(c))));
            } else {
              path.push_back(expr_to_node(c));
            }
          }
        }
      } else if (t == "member_selection") {
        TSNode arg = child_by_field(n, "argument");
        if (!ts_node_is_null(arg)) {
          collect(arg);
        }
        uint32_t nnc = ts_node_named_child_count(n);
        for (uint32_t i = 0; i < nnc; i++) {
          TSNode c = ts_node_named_child(n, i);
          if (!ts_node_is_null(arg) && ts_node_eq(c, arg)) {
            continue;
          }
          // Each `select` carries either a list of indices, or an
          // open/from-zero range. Only the list form is supported as an
          // lvalue path today.
          TSNode list = child_by_field(c, "list");
          if (!ts_node_is_null(list)) {
            uint32_t lnnc = ts_node_named_child_count(list);
            for (uint32_t j = 0; j < lnnc; j++) {
              path.push_back(expr_to_node(ts_node_named_child(list, j)));
            }
          }
        }
      } else if (t == "identifier") {
        root      = identifier_to_node(n, /*for_lvalue=*/true);
        have_root = true;
      } else {
        root      = expr_to_node(n);
        have_root = true;
      }
    };
    collect(lvalue);
    if (have_root) {
      auto idx = builder.add_child(Lnast_ntype::create_tuple_set());
      lnast->add_child(idx, root);
      for (auto& p : path) {
        lnast->add_child(idx, p);
      }
      lnast->add_child(idx, rvalue);
      return root;
    }
  }
  // Fallback: treat as a single assign using the text span.
  auto       name = trim(get_text(lvalue));
  Lnast_node ref  = Lnast_node::create_ref(name);
  auto       aidx = builder.add_child(Lnast_ntype::create_assign());
  lnast->add_child(aidx, ref);
  lnast->add_child(aidx, rvalue);
  return ref;
}

void Prp2lnast::process_assignment(TSNode n) {
  TSNode decl = child_by_field(n, "decl");
  TSNode lv   = child_by_field(n, "lvalue");
  TSNode op   = child_by_field(n, "operator");
  TSNode rv   = child_by_field(n, "rvalue");
  TSNode tc   = child_by_field(n, "type");  // outer type_cast on complex lvalue

  // ── Comptime tuple-shape tracking ─────────────────────────────────────
  //
  // Used by process_for_statement to unroll `for (…) in NAME` (with values)
  // and `for i in ref NAME` (sized, with write-back). See header for the
  // recording rules. This block runs on every assignment, and either:
  //   - records / extends comptime_tuples_[NAME], or
  //   - invalidates it (any non-trackable write to NAME).
  // It must run before the rvalue is lowered: the rvalue handler may emit
  // its own assigns to other names, but those won't disturb our LHS.
  auto get_simple_lvalue_name = [&]() -> std::string {
    if (ts_node_is_null(lv)) {
      return {};
    }
    std::string_view lvt(ts_node_type(lv));
    if (lvt == "typed_identifier") {
      TSNode id = child_by_field(lv, "identifier");
      if (!ts_node_is_null(id)) {
        return std::string(trim(get_text(id)));
      }
    } else if (lvt == "identifier") {
      return std::string(trim(get_text(lv)));
    }
    return {};
  };

  // Walk a tuple-literal TSNode and either fully extract entries (every slot
  // a constant or `name=const`) or report what we know structurally — the
  // count and key shape — with `value_known=false` so callers know the
  // values aren't statically resolvable. Returns nullopt only when the shape
  // itself is unrecognized (spread, decl-form item, nested expressions in
  // unexpected positions).
  auto extract_tuple_shape = [&](TSNode tup) -> std::optional<std::vector<Comptime_tuple_entry>> {
    if (ts_node_is_null(tup) || std::string_view(ts_node_type(tup)) != "tuple") {
      return std::nullopt;
    }
    std::vector<Comptime_tuple_entry> entries;
    uint32_t                          nnc = ts_node_named_child_count(tup);
    for (uint32_t i = 0; i < nnc; i++) {
      TSNode               c = ts_node_named_child(tup, i);
      std::string          ct(ts_node_type(c));
      Comptime_tuple_entry ent;
      if (ct == "assignment") {
        TSNode it_lv = child_by_field(c, "lvalue");
        TSNode it_rv = child_by_field(c, "rvalue");
        TSNode it_op = child_by_field(c, "operator");
        if (ts_node_is_null(it_lv)) {
          return std::nullopt;
        }
        std::string_view it_lvt(ts_node_type(it_lv));
        if (it_lvt == "typed_identifier") {
          TSNode it_id = child_by_field(it_lv, "identifier");
          if (ts_node_is_null(it_id)) {
            return std::nullopt;
          }
          ent.key = trim(get_text(it_id));
        } else if (it_lvt == "identifier") {
          ent.key = trim(get_text(it_lv));
        } else {
          return std::nullopt;
        }
        if (ts_node_is_null(it_rv)) {
          if (ts_node_is_null(it_op)) {
            return std::nullopt;
          }
          ent.value_text  = std::string(trim(text_between(ts_node_end_byte(it_op), ts_node_end_byte(c))));
          ent.value_known = !ent.value_text.empty();
        } else if (std::string_view(ts_node_type(it_rv)) == "constant") {
          ent.value_text  = trim(get_text(it_rv));
          ent.value_known = true;
        } else {
          // Non-literal value (identifier, expression). Slot exists; value
          // unknown.
          ent.value_known = false;
        }
      } else if (ct == "constant") {
        ent.value_text  = trim(get_text(c));
        ent.value_known = true;
      } else if (ct == "identifier") {
        // Bare positional identifier — slot exists, value unknown.
        ent.value_known = false;
      } else if (ct == "unary_expression") {
        // `...spread` would change size dynamically; bail.
        return std::nullopt;
      } else {
        // Other expressions: treat as positional unknown rather than bailing,
        // so `(i,)` from an unrolled loop still grows the size by 1.
        ent.value_known = false;
      }
      entries.push_back(std::move(ent));
    }
    return entries;
  };

  // Detect `lhs = lhs ++ <tuple_literal>` (also `lhs ++= <tuple_literal>`).
  // The compound `++=` form short-circuits via `op_text` below; here we only
  // look at the explicit-form rvalue: an `expression_item` of (lhs_name,
  // binary_other_op `++`, tuple_literal).
  auto is_self_concat_with_tuple = [&](const std::string& lhs_name, TSNode rvn) -> std::optional<TSNode> {
    if (ts_node_is_null(rvn)) {
      return std::nullopt;
    }
    if (std::string_view(ts_node_type(rvn)) != "expression_item") {
      return std::nullopt;
    }
    if (ts_node_named_child_count(rvn) != 3) {
      return std::nullopt;
    }
    TSNode lhs_id = ts_node_named_child(rvn, 0);
    TSNode op_n   = ts_node_named_child(rvn, 1);
    TSNode rhs_n  = ts_node_named_child(rvn, 2);
    if (std::string_view(ts_node_type(lhs_id)) != "identifier") {
      return std::nullopt;
    }
    if (trim(get_text(lhs_id)) != lhs_name) {
      return std::nullopt;
    }
    if (std::string_view(ts_node_type(op_n)) != "binary_other_op") {
      return std::nullopt;
    }
    // The binary_other_op wrapper has a single named child of the aliased op.
    TSNode op_inner = ts_node_named_child(op_n, 0);
    if (ts_node_is_null(op_inner) || std::string_view(ts_node_type(op_inner)) != "op_tuple_concat") {
      return std::nullopt;
    }
    if (std::string_view(ts_node_type(rhs_n)) != "tuple") {
      return std::nullopt;
    }
    return rhs_n;
  };

  // The assignment_operator wrapper has a single named child of the aliased
  // op kind (`assign`, `assign_add`, …). Drive both the comptime-shape rules
  // and the compound-op lowering off that, no text compares.
  std::string_view op_kind;
  if (!ts_node_is_null(op)) {
    TSNode op_inner = ts_node_named_child(op, 0);
    op_kind         = ts_node_is_null(op_inner) ? std::string_view{} : std::string_view(ts_node_type(op_inner));
  } else {
    op_kind = "assign";  // assignment without an explicit operator defaults to plain `=`
  }

  // Handle the recording rules in source order.
  std::string lhs_name = get_simple_lvalue_name();
  if (!lhs_name.empty()) {
    bool has_decl           = !ts_node_is_null(decl);
    bool is_compound_concat = op_kind == "assign_tuple_concat";
    bool is_plain_assign    = op_kind == "assign";

    auto invalidate = [&]() { comptime_tuples_.erase(lhs_name); };

    if (has_decl && is_plain_assign) {
      // Declaration assignments always rebind the slot. `mut/const NAME =
      // <tuple_literal>` populates the shape; anything else clears stale
      // tracking.
      bool recorded = false;
      if (!ts_node_is_null(rv) && std::string_view(ts_node_type(rv)) == "tuple") {
        if (auto entries = extract_tuple_shape(rv)) {
          comptime_tuples_[lhs_name] = std::move(*entries);
          recorded                   = true;
        }
      }
      if (!recorded) {
        invalidate();
      }
    } else if (is_compound_concat) {
      // `NAME ++= <tuple_literal>` → if we know NAME's shape, append.
      auto it = comptime_tuples_.find(lhs_name);
      if (it != comptime_tuples_.end() && !ts_node_is_null(rv) && std::string_view(ts_node_type(rv)) == "tuple") {
        if (auto extra = extract_tuple_shape(rv)) {
          for (auto& e : *extra) {
            it->second.push_back(std::move(e));
          }
        } else {
          invalidate();
        }
      } else if (it != comptime_tuples_.end()) {
        invalidate();
      }
    } else if (is_plain_assign) {
      // `NAME = …`. Two trackable sub-cases:
      //   1. `NAME = NAME ++ <tuple_literal>` — append the rhs entries.
      //   2. `NAME = <tuple_literal>` — rebind to that shape (only if NAME
      //      was already tracked, otherwise stay agnostic to avoid creating
      //      false comptime entries for plain mut-rebinds).
      auto it = comptime_tuples_.find(lhs_name);
      if (auto rhs_tup = is_self_concat_with_tuple(lhs_name, rv)) {
        if (it != comptime_tuples_.end()) {
          if (auto extra = extract_tuple_shape(*rhs_tup)) {
            for (auto& e : *extra) {
              it->second.push_back(std::move(e));
            }
          } else {
            invalidate();
          }
        }
      } else if (it != comptime_tuples_.end() && !ts_node_is_null(rv) && std::string_view(ts_node_type(rv)) == "tuple") {
        if (auto entries = extract_tuple_shape(rv)) {
          it->second = std::move(*entries);
        } else {
          invalidate();
        }
      } else if (it != comptime_tuples_.end()) {
        invalidate();
      }
    } else {
      // Compound op other than `++=` (e.g. `+=`, `*=`) on a tracked name
      // changes the value semantics in ways we can't model cheaply.
      if (comptime_tuples_.contains(lhs_name)) {
        comptime_tuples_.erase(lhs_name);
      }
    }
  }

  // The new grammar attaches a statement-level `wrap`/`sat` prefix as the
  // `overflow` field of the enclosing _statement. Lower it as an attr_set
  // bound to the lvalue. Consumed once per assignment.
  const std::string_view overflow_kind = pending_overflow_kind;
  pending_overflow_kind                = {};

  // Get rvalue node or fallback text for hidden tokens
  Lnast_node rvalue_node;
  if (ts_node_is_null(rv)) {
    // Hidden rvalue (constant). Extract text after operator.
    auto op_end  = ts_node_end_byte(op);
    auto par_end = ts_node_end_byte(n);
    auto text    = trim(text_between(op_end, par_end));
    rvalue_node  = constant_text_to_node(text);
  } else {
    rvalue_node = expr_to_node(rv);
  }

  // Compound assignment: lower `a OP= b` to `OP tmp a b; assign a tmp`.
  // The aliased kind on the assignment_operator child names the underlying op.
  if (op_kind != "assign") {
    using Factory                                                                = Lnast_ntype::Lnast_ntype_int (*)();
    static const absl::flat_hash_map<std::string_view, Factory> compound_factory = {
        {"assign_add", [] { return Lnast_ntype::create_plus(); }},
        {"assign_sub", [] { return Lnast_ntype::create_minus(); }},
        {"assign_mul", [] { return Lnast_ntype::create_mult(); }},
        {"assign_div", [] { return Lnast_ntype::create_div(); }},
        {"assign_bit_or", [] { return Lnast_ntype::create_bit_or(); }},
        {"assign_bit_and", [] { return Lnast_ntype::create_bit_and(); }},
        {"assign_bit_xor", [] { return Lnast_ntype::create_bit_xor(); }},
        {"assign_shl", [] { return Lnast_ntype::create_shl(); }},
        {"assign_sra", [] { return Lnast_ntype::create_sra(); }},
        {"assign_tuple_concat", [] { return Lnast_ntype::create_tuple_concat(); }},
        {"assign_log_or", [] { return Lnast_ntype::create_log_or(); }},
        {"assign_log_and", [] { return Lnast_ntype::create_log_and(); }},
    };
    auto fit              = compound_factory.find(op_kind);
    auto compound_op_type = (fit == compound_factory.end()) ? Lnast_ntype::create_invalid() : fit->second();
    if (Lnast_ntype::is_invalid(compound_op_type)) {
      std::print("prp2lnast: unhandled compound op `{}`\n", op_kind);
      return;
    }
    Lnast_node left_ref = expr_to_node(lv);
    Lnast_node result   = builder.mint_tmp_ref();
    auto       idx      = builder.add_child(compound_op_type);
    lnast->add_child(idx, result);
    lnast->add_child(idx, left_ref);
    lnast->add_child(idx, rvalue_node);
    (void)process_lvalue_for_assign(lv, result, decl, tc);
    return;
  }

  Lnast_node lhs_ref = process_lvalue_for_assign(lv, rvalue_node, decl, tc);

  if (!overflow_kind.empty()) {
    auto idx = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(idx, lhs_ref);
    lnast->add_child(idx, Lnast_node::create_const(overflow_kind));
    lnast->add_child(idx, Lnast_node::create_const("true"));
  }
}

// ---------------- Assert ----------------

void Prp2lnast::process_assert_statement(TSNode n) {
  TSNode cond = child_by_field(n, "condition");
  if (ts_node_is_null(cond)) {
    // Hidden condition — extract text after the `cassert`/`assert` keyword.
    // Find the 'condition' start position: the first non-'always'/assert token.
    uint32_t start = 0;
    for (uint32_t i = 0; i < child_count(n); i++) {
      TSNode           c   = child(n, i);
      std::string_view txt = get_text(c);
      if (txt == "always" || txt == "assert" || txt == "cassert") {
        start = ts_node_end_byte(c);
      } else {
        break;
      }
    }
    auto txt = trim(text_between(start, ts_node_end_byte(n)));
    auto idx = builder.add_child(Lnast_ntype::create_cassert());
    lnast->add_child(idx, constant_text_to_node(txt));
    return;
  }
  Lnast_node cond_ref = expr_to_node(cond);
  auto       idx      = builder.add_child(Lnast_ntype::create_cassert());
  lnast->add_child(idx, cond_ref);
}

// ---------------- Control Flow ----------------

void Prp2lnast::process_while_statement(TSNode n) {
  TSNode cond = child_by_field(n, "condition");
  TSNode code = child_by_field(n, "code");
  TSNode init = child_by_field(n, "init");

  if (!ts_node_is_null(init)) {
    // Init stmt_list hoists outside
    uint32_t nnc = ts_node_named_child_count(init);
    for (uint32_t i = 0; i < nnc; i++) {
      process_statement(ts_node_named_child(init, i));
    }
  }

  Lnast_node cond_ref;
  if (ts_node_is_null(cond)) {
    cond_ref = Lnast_node::create_const("true");
  } else {
    cond_ref = expr_to_node(cond);
  }
  auto while_idx = builder.add_child(Lnast_ntype::create_while());
  lnast->add_child(while_idx, cond_ref);
  auto body_idx = lnast->add_child(while_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, body_idx);
  }
  builder.pop_stmts();
}

void Prp2lnast::process_for_statement(TSNode n) {
  // Pyrope `for` is comptime-only: every for-loop's iteration count must be
  // resolvable at parse time so we can emit an unrolled sequence of nested
  // stmts blocks. Each iteration block re-binds the iter variable and runs
  // a fresh emission of the body. Block scoping (see Symbol_table::block_scope)
  // gives every iteration its own scope, so user-name shadowing and temp
  // collisions are handled automatically — no per-iter renaming pass needed.
  //
  // Lowering shape, e.g. `for i in 1..<4 { … }`:
  //
  //   stmts (outer wrap)
  //     attr_set i type mut
  //     assign   i nil
  //     stmts (iter 0)
  //       assign i 1
  //       <body>
  //     stmts (iter 1)
  //       assign i 2
  //       <body>
  //     stmts (iter 2)
  //       assign i 3
  //       <body>
  //
  // Bare `i = k` resolves to the outer mut via Symbol_table's parent walk.
  // Bare writes to outer-scope variables inside the body work the same way,
  // so `c = c ++ (i,)` (loop_equivalent) updates the caller's `c` correctly.
  //
  // For non-comptime ranges we fall back to the legacy placeholder while
  // loop so existing tests that don't actually need to unroll still parse.

  TSNode code = child_by_field(n, "code");
  TSNode data = child_by_field(n, "data");
  if (ts_node_is_null(data)) {
    // ref_identifier form — find and use it as data
    for (uint32_t i = 0; i < child_count(n); i++) {
      TSNode           c = child(n, i);
      std::string_view t(ts_node_type(c));
      if (t == "ref_identifier") {
        data = c;
        break;
      }
    }
  }
  // Find binding: first typed_identifier or typed_identifier_list under arg_list
  TSNode binding = ts_node_child_by_field_name(n, "index", 5);
  if (ts_node_is_null(binding)) {
    for (uint32_t i = 0; i < child_count(n); i++) {
      TSNode           c = child(n, i);
      std::string_view t(ts_node_type(c));
      if (t == "typed_identifier") {
        binding = c;
        break;
      }
    }
  }

  // Try to detect a comptime literal range under data: an expression_item
  // shaped as `<int_const> ..<|..= <int_const>` (possibly nested in an
  // expression_list/expression_item). Returns the (start, end_inclusive)
  // pair on success.
  auto parse_int_const = [&](TSNode c) -> std::optional<int64_t> {
    if (ts_node_is_null(c)) {
      return std::nullopt;
    }
    std::string_view t(ts_node_type(c));
    if (t != "constant") {
      return std::nullopt;
    }
    auto txt = trim(get_text(c));
    if (txt.empty()) {
      return std::nullopt;
    }
    try {
      size_t  pos = 0;
      int64_t v   = std::stoll(std::string(txt), &pos);
      // Reject suffixes / non-decimal forms; we only handle plain ints.
      if (pos != txt.size()) {
        return std::nullopt;
      }
      return v;
    } catch (...) {
      return std::nullopt;
    }
  };

  // Walk a data expression looking for the literal-range pattern. Accepts
  // `expression_list { item: expression_item { const, ..<|..=, const } }`
  // and the bare expression_item form.
  auto try_parse_literal_range = [&](TSNode d) -> std::optional<std::tuple<int64_t, int64_t, bool>> {
    if (ts_node_is_null(d)) {
      return std::nullopt;
    }
    std::string_view dt(ts_node_type(d));
    TSNode           inner = d;
    if (dt == "expression_list") {
      // Reject multi-item lists — `for i in a, b` isn't a range.
      uint32_t count = 0;
      TSNode   first;
      for (uint32_t i = 0; i < child_count(d); i++) {
        TSNode      c  = child(d, i);
        const char* fn = ts_node_field_name_for_child(d, i);
        if (fn && std::string_view(fn) == "item") {
          if (count == 0) {
            first = c;
          }
          ++count;
        }
      }
      if (count != 1) {
        return std::nullopt;
      }
      inner = first;
      dt    = std::string_view(ts_node_type(inner));
    }
    if (dt != "expression_item") {
      return std::nullopt;
    }
    // Expect exactly: const, binary_other_op(`op_range_inclusive` or
    // `op_range_exclusive`), const.
    TSNode lhs, op, rhs;
    int    seen_named = 0;
    for (uint32_t i = 0; i < ts_node_named_child_count(inner); i++) {
      TSNode c = ts_node_named_child(inner, i);
      if (seen_named == 0) {
        lhs = c;
      } else if (seen_named == 1) {
        op = c;
      } else if (seen_named == 2) {
        rhs = c;
      }
      ++seen_named;
    }
    if (seen_named != 3) {
      return std::nullopt;
    }
    if (std::string_view(ts_node_type(op)) != "binary_other_op") {
      return std::nullopt;
    }
    TSNode op_inner = ts_node_named_child(op, 0);
    if (ts_node_is_null(op_inner)) {
      return std::nullopt;
    }
    std::string_view op_kind(ts_node_type(op_inner));
    bool             inclusive;
    if (op_kind == "op_range_inclusive") {
      inclusive = true;
    } else if (op_kind == "op_range_exclusive") {
      inclusive = false;
    } else {
      return std::nullopt;
    }
    auto lo = parse_int_const(lhs);
    auto hi = parse_int_const(rhs);
    if (!lo || !hi) {
      return std::nullopt;
    }
    int64_t end_incl = inclusive ? *hi : (*hi - 1);
    return std::make_tuple(*lo, end_incl, true);
  };

  auto literal_range = try_parse_literal_range(data);

  // Collect 1..N binding ids. `for i in …` parses as a single typed_identifier;
  // `for (e, idx, key) in …` parses as typed_identifier_list with 1–3 items
  // (value, position, key). Anything else falls back to the placeholder while.
  std::vector<TSNode> bind_ids;
  if (!ts_node_is_null(binding)) {
    std::string_view bt(ts_node_type(binding));
    if (bt == "typed_identifier") {
      TSNode id = child_by_field(binding, "identifier");
      if (!ts_node_is_null(id)) {
        bind_ids.push_back(id);
      }
    } else if (bt == "typed_identifier_list") {
      uint32_t nnc = ts_node_named_child_count(binding);
      for (uint32_t i = 0; i < nnc && bind_ids.size() < 3; i++) {
        TSNode           item = ts_node_named_child(binding, i);
        std::string_view it(ts_node_type(item));
        if (it == "typed_identifier") {
          TSNode id = child_by_field(item, "identifier");
          if (!ts_node_is_null(id)) {
            bind_ids.push_back(id);
          }
        }
      }
    }
  }
  TSNode bind_id;
  if (!bind_ids.empty()) {
    bind_id = bind_ids.front();
  }

  if (literal_range && !ts_node_is_null(bind_id) && !ts_node_is_null(code)) {
    auto [lo, hi_incl, ok] = *literal_range;
    (void)ok;
    // Outer wrap stmts so the iter mut isn't visible to siblings.
    auto outer_idx = builder.add_child(Lnast_ntype::create_stmts());
    builder.push_stmts(outer_idx);

    Lnast_node iter_ref = identifier_to_node(bind_id, true);
    {
      auto attr = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(attr, iter_ref);
      lnast->add_child(attr, Lnast_node::create_const("type"));
      lnast->add_child(attr, Lnast_node::create_const("mut"));
      auto a = builder.add_child(Lnast_ntype::create_assign());
      lnast->add_child(a, iter_ref);
      lnast->add_child(a, Lnast_node::create_const("nil"));
    }

    // Empty range (lo > hi_incl) emits no iteration blocks. We still
    // declare the iter mut so the wrap remains a valid stmts.
    for (int64_t k = lo; k <= hi_incl; ++k) {
      auto iter_stmts = builder.add_child(Lnast_ntype::create_stmts());
      builder.push_stmts(iter_stmts);
      // Bare `i = k` — Symbol_table::set walks up to the outer mut declared
      // above, so each iteration overwrites the same slot rather than
      // shadowing. Emitting `assign` (no attr_set) keeps the resolution
      // out-of-scope rather than re-declaring locally.
      auto a = builder.add_child(Lnast_ntype::create_assign());
      lnast->add_child(a, iter_ref);
      lnast->add_child(a, Lnast_node::create_const(std::to_string(k)));
      // Re-walk the body TSNode each iter — builder.mint_tmp_ref counter advances,
      // so emitted temps (`___N`) get fresh names per iteration. User
      // names (mut x = …) get fresh per-iter declarations because each
      // iter's stmts is a new scope.
      process_scope_statement(code, iter_stmts);
      builder.pop_stmts();
    }
    builder.pop_stmts();
    return;
  }

  // Comptime-tuple unroll: `for (e[, idx[, key]]) in [ref] NAME { … }` when
  // NAME's shape was tracked by process_assignment (see comptime_tuples_).
  //
  //   - The value binding (refs[0]) is read via `tuple_get NAME pos` each
  //     iteration so it works whether or not the per-slot value is known
  //     statically (handles both `const t1 = (a=1,b=2,c=3)` and a tuple
  //     built up by a prior unrolled `for i in 0..<N { d = d ++ (i,) }`).
  //   - idx (refs[1]) is the position const; key (refs[2]) is the tracked
  //     slot name (empty string when the slot is purely positional).
  //   - When the source is `ref NAME`, we emit `tuple_set NAME pos refs[0]`
  //     after the body so user mutations to the binding flow back into NAME
  //     (mirrors the source-level `i = NAME[pos]; <body>; NAME[pos] = i`
  //     equivalent).
  if (!literal_range && !bind_ids.empty() && !ts_node_is_null(code) && !ts_node_is_null(data)) {
    TSNode           src    = data;
    bool             is_ref = false;
    std::string_view src_top(ts_node_type(src));
    if (src_top == "ref_identifier") {
      // ref_identifier := 'ref' _complex_identifier — drill to the inner id.
      is_ref      = true;
      uint32_t nc = ts_node_named_child_count(src);
      if (nc >= 1) {
        src = ts_node_named_child(src, 0);
      }
    }
    while (true) {
      std::string_view st(ts_node_type(src));
      if (st == "ref_identifier" || st == "identifier") {
        break;
      }
      if (st == "expression_list" || st == "expression_item") {
        if (ts_node_named_child_count(src) == 1) {
          src = ts_node_named_child(src, 0);
          continue;
        }
      }
      break;
    }
    std::string src_name;
    {
      std::string_view st(ts_node_type(src));
      if (st == "ref_identifier" || st == "identifier") {
        src_name = trim(get_text(src));
      }
    }
    auto it = src_name.empty() ? comptime_tuples_.end() : comptime_tuples_.find(src_name);
    if (it != comptime_tuples_.end()) {
      // Snapshot entries by value: process_scope_statement may emit
      // assignments that mutate comptime_tuples_[src_name] as a side effect
      // (the `mut d = ()` / `d = d ++ (i,)` tracking in the body would grow
      // d while we're iterating its current shape). Iterating a stale
      // snapshot keeps the unroll bounded.
      const auto entries = it->second;

      auto outer_idx = builder.add_child(Lnast_ntype::create_stmts());
      builder.push_stmts(outer_idx);

      std::vector<Lnast_node> refs;
      refs.reserve(bind_ids.size());
      for (TSNode bid : bind_ids) {
        Lnast_node r = identifier_to_node(bid, true);
        refs.push_back(r);
        auto attr = builder.add_child(Lnast_ntype::create_attr_set());
        lnast->add_child(attr, r);
        lnast->add_child(attr, Lnast_node::create_const("type"));
        lnast->add_child(attr, Lnast_node::create_const("mut"));
        auto a = builder.add_child(Lnast_ntype::create_assign());
        lnast->add_child(a, r);
        lnast->add_child(a, Lnast_node::create_const("nil"));
      }

      Lnast_node src_ref = Lnast_node::create_ref(src_name);

      for (size_t i = 0; i < entries.size(); ++i) {
        const auto& ent        = entries[i];
        auto        iter_stmts = builder.add_child(Lnast_ntype::create_stmts());
        builder.push_stmts(iter_stmts);

        // refs[0] = NAME[pos]. Indexed read keeps this branch generic across
        // value-known (const tuple) and value-unknown (loop-built) shapes.
        if (refs.size() >= 1) {
          auto tg = builder.add_child(Lnast_ntype::create_tuple_get());
          lnast->add_child(tg, refs[0]);
          lnast->add_child(tg, src_ref);
          lnast->add_child(tg, Lnast_node::create_const(std::to_string(i)));
        }
        if (refs.size() >= 2) {
          auto a = builder.add_child(Lnast_ntype::create_assign());
          lnast->add_child(a, refs[1]);
          lnast->add_child(a, Lnast_node::create_const(std::to_string(i)));
        }
        if (refs.size() >= 3) {
          auto a = builder.add_child(Lnast_ntype::create_assign());
          lnast->add_child(a, refs[2]);
          // String literal so constprop's tuple_set field-resolution sees a
          // string trivial, not a bareword ref. Empty key (positional slot)
          // becomes the empty-string literal.
          lnast->add_child(a, Lnast_node::create_const("'" + ent.key + "'"));
        }

        process_scope_statement(code, iter_stmts);

        // Write back through the value binding when the source is `ref`.
        // `for i in ref d { i += 10 }` lowers per-iter to a `tuple_set d pos
        // i` after the body, so any in-body mutation of `i` propagates into
        // d's slot. Skipped for the read-only `for i in d` form.
        if (is_ref && refs.size() >= 1) {
          auto ts = builder.add_child(Lnast_ntype::create_tuple_set());
          lnast->add_child(ts, src_ref);
          lnast->add_child(ts, Lnast_node::create_const(std::to_string(i)));
          lnast->add_child(ts, refs[0]);
        }

        builder.pop_stmts();
      }
      builder.pop_stmts();
      return;
    }
  }

  // Fallback: legacy placeholder while-loop. Keeps existing parser-only
  // tests building until we extend the unroll to range expressions whose
  // bounds need constprop folding to resolve.
  auto while_idx = builder.add_child(Lnast_ntype::create_while());
  lnast->add_child(while_idx, Lnast_node::create_const("true"));
  auto body_idx = lnast->add_child(while_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);

  if (!ts_node_is_null(binding) && !ts_node_is_null(data)) {
    std::string_view t(ts_node_type(binding));
    if (t == "typed_identifier") {
      TSNode id = child_by_field(binding, "identifier");
      if (!ts_node_is_null(id)) {
        Lnast_node ref = identifier_to_node(id, true);
        auto       idx = builder.add_child(Lnast_ntype::create_tuple_get());
        lnast->add_child(idx, ref);
        lnast->add_child(idx, expr_to_node(data));
        lnast->add_child(idx, Lnast_node::create_const("0"));
      }
    }
  }
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, body_idx);
  }

  builder.add_child(Lnast_node::create_invalid());  // sentinel (invalid never emitted normally)
  builder.pop_stmts();
}

void Prp2lnast::process_loop_statement(TSNode n) {
  // loop { body } -> while true body
  TSNode code      = child_by_field(n, "code");
  auto   while_idx = builder.add_child(Lnast_ntype::create_while());
  lnast->add_child(while_idx, Lnast_node::create_const("true"));
  auto body_idx = lnast->add_child(while_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, body_idx);
  }
  builder.pop_stmts();
}

void Prp2lnast::process_control_statement(TSNode n) {
  // control_statement now wraps a single named child of break_statement,
  // continue_statement, or return_statement — dispatch on its node kind.
  TSNode inner = ts_node_named_child(n, 0);
  if (ts_node_is_null(inner)) {
    return;
  }
  std::string_view it(ts_node_type(inner));
  auto             emit_marker = [&](Lnast_ntype::Lnast_ntype_int head, Lnast_node arg) {
    auto idx = builder.add_child(head);
    lnast->add_child(idx, builder.mint_tmp_ref());
    if (!arg.is_invalid()) {
      lnast->add_child(idx, arg);
    }
  };
  if (it == "break_statement") {
    emit_marker(Lnast_ntype::create_func_break(), Lnast_node::create_invalid());
    return;
  }
  if (it == "continue_statement") {
    emit_marker(Lnast_ntype::create_func_continue(), Lnast_node::create_invalid());
    return;
  }
  TSNode arg = child_by_field(inner, "argument");
  emit_marker(Lnast_ntype::create_func_return(), ts_node_is_null(arg) ? Lnast_node::create_invalid() : expr_to_node(arg));
}

std::vector<Prp2lnast::Call_arg> Prp2lnast::collect_call_args(TSNode arg_tuple) {
  std::vector<Call_arg> call_args;
  if (ts_node_is_null(arg_tuple)) {
    return call_args;
  }

  uint32_t nnc = ts_node_named_child_count(arg_tuple);
  call_args.reserve(nnc);
  for (uint32_t i = 0; i < nnc; ++i) {
    TSNode           c = ts_node_named_child(arg_tuple, i);
    std::string_view t(ts_node_type(c));

    Call_arg arg;
    if (t == "assignment") {
      arg.is_assign = true;
      TSNode lv     = child_by_field(c, "lvalue");
      TSNode rv     = child_by_field(c, "rvalue");

      std::string_view lvt(ts_node_type(lv));
      if (lvt == "typed_identifier") {
        TSNode id = child_by_field(lv, "identifier");
        if (!ts_node_is_null(id)) {
          arg.assign_key = trim(get_text(id));
        }
      } else {
        arg.assign_key = trim(get_text(lv));
      }

      if (arg.assign_key.empty()) {
        arg.assign_key = builder.create_lnast_tmp();
      }

      if (!ts_node_is_null(rv)) {
        arg.value = expr_to_node(rv);
      } else {
        TSNode op    = child_by_field(c, "operator");
        auto   start = ts_node_is_null(op) ? ts_node_end_byte(lv) : ts_node_end_byte(op);
        arg.value    = constant_text_to_node(trim(text_between(start, ts_node_end_byte(c))));
      }
    } else {
      arg.value = expr_to_node(c);
    }
    call_args.emplace_back(std::move(arg));
  }

  if (call_args.empty() && ts_node_child_count(arg_tuple) > 0) {
    auto inner = trim(text_between(ts_node_start_byte(arg_tuple) + 1, ts_node_end_byte(arg_tuple) - 1));
    if (!inner.empty()) {
      Call_arg arg;
      arg.value = constant_text_to_node(inner);
      call_args.emplace_back(std::move(arg));
    }
  }

  return call_args;
}

void Prp2lnast::add_call_args_to_fcall(const Lnast_nid& fcall_idx, const std::vector<Call_arg>& call_args) {
  for (const auto& arg : call_args) {
    if (arg.is_assign) {
      auto aidx = lnast->add_child(fcall_idx, Lnast_ntype::create_assign());
      lnast->add_child(aidx, Lnast_node::create_ref(arg.assign_key));
      lnast->add_child(aidx, arg.value);
    } else {
      lnast->add_child(fcall_idx, arg.value);
    }
  }
}

void Prp2lnast::process_function_call_statement(TSNode n) {
  TSNode     func = child_by_field(n, "function");
  TSNode     args = child_by_field(n, "argument");
  Lnast_node func_ref;
  if (ts_node_is_null(func)) {
    func_ref = Lnast_node::create_const("nil");
  } else {
    auto name = trim(get_text(func));
    func_ref  = Lnast_node::create_ref(name);
  }
  auto idx = builder.add_child(Lnast_ntype::create_func_call());
  lnast->add_child(idx, builder.mint_tmp_ref());
  lnast->add_child(idx, func_ref);
  add_call_args_to_fcall(idx, collect_call_args(args));
}

void Prp2lnast::process_lambda_statement(TSNode n) {
  TSNode func_type_node = child_by_field(n, "func_type");
  TSNode name_node      = child_by_field(n, "name");
  // function_definition_decl is a named child that isn't directly a field
  TSNode fdef;
  for (uint32_t i = 0; i < ts_node_named_child_count(n); i++) {
    TSNode           c = ts_node_named_child(n, i);
    std::string_view t(ts_node_type(c));
    if (t == "function_definition_decl") {
      fdef = c;
      break;
    }
  }
  TSNode code = child_by_field(n, "code");

  std::string_view kind = ts_node_is_null(func_type_node) ? std::string_view{"comb"} : trim(get_text(func_type_node));
  // Strip optional pipe[N] attribute. The new grammar adds `proc` as a
  // recognized lambda kind; treat it as-is (no normalization).
  if (kind.size() >= 4 && kind.substr(0, 4) == "pipe") {
    kind = "pipe";
  }

  // Workaround for tree-sitter not always attaching the body to `code`:
  // `comb mytest(a,b) -> (r) { ... }` parses as a `lambda` node followed
  // by a sibling `scope_statement` instead of attaching the block to the
  // lambda's `code` field. When we detect that, adopt the sibling as the
  // body and mark its start byte so the enclosing walker
  // (process_description / process_scope_statement) skips it.
  if (ts_node_is_null(code)) {
    for (TSNode sib = ts_node_next_named_sibling(n); !ts_node_is_null(sib); sib = ts_node_next_named_sibling(sib)) {
      std::string_view st(ts_node_type(sib));
      if (st == "comment") {
        continue;
      }
      if (st == "scope_statement") {
        code = sib;
        consumed_lambda_body_starts.insert(ts_node_start_byte(sib));
      }
      break;
    }
  }

  Lnast_node lambda_ref = ts_node_is_null(name_node) ? builder.mint_tmp_ref() : Lnast_node::create_ref(get_text(name_node));

  auto fd_idx = builder.add_child(Lnast_ntype::create_func_def());
  lnast->add_child(fd_idx, lambda_ref);
  lnast->add_child(fd_idx, Lnast_node::create_const(kind));
  // generics tuple (empty)
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  // captures tuple (empty)
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());

  // Emit input args
  auto in_idx = lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  if (!ts_node_is_null(fdef)) {
    TSNode inp = child_by_field(fdef, "input");
    if (!ts_node_is_null(inp)) {
      uint32_t nnc = ts_node_named_child_count(inp);
      for (uint32_t i = 0; i < nnc; i++) {
        TSNode ti = ts_node_named_child(inp, i);
        TSNode id = child_by_field(ti, "identifier");
        if (ts_node_is_null(id)) {
          continue;
        }
        auto aidx = lnast->add_child(in_idx, Lnast_ntype::create_assign());
        lnast->add_child(aidx, Lnast_node::create_ref(get_text(id)));
        lnast->add_child(aidx, Lnast_node::create_const("0"));
      }
    }
  }

  // Output args
  auto                     out_idx = lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  std::vector<std::string> output_refs;  // for placeholder-lambda implicit assign
  if (!ts_node_is_null(fdef)) {
    // Collect ALL output field occurrences (there may be multiple due to "->" anchor + arg_list form)
    auto emit_output_id = [&](TSNode id) {
      if (ts_node_is_null(id)) {
        return;
      }
      auto txt = get_text(id);
      output_refs.emplace_back(txt);
      auto aidx = lnast->add_child(out_idx, Lnast_ntype::create_assign());
      lnast->add_child(aidx, Lnast_node::create_ref(txt));
      lnast->add_child(aidx, Lnast_node::create_const("0"));
    };
    for (uint32_t i = 0; i < child_count(fdef); i++) {
      const char* fname = ts_node_field_name_for_child(fdef, i);
      if (!fname || std::string_view(fname) != "output") {
        continue;
      }
      TSNode           o = child(fdef, i);
      std::string_view ot(ts_node_type(o));
      if (ot == "arg_list") {
        uint32_t nnc = ts_node_named_child_count(o);
        for (uint32_t j = 0; j < nnc; j++) {
          TSNode ti = ts_node_named_child(o, j);
          emit_output_id(child_by_field(ti, "identifier"));
        }
      } else if (ot == "typed_identifier") {
        emit_output_id(child_by_field(o, "identifier"));
      }
    }
  }

  // Body
  auto body_idx = lnast->add_child(fd_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);
  if (!ts_node_is_null(code)) {
    // Placeholder lambda (06-functions.md "Output tuple"): when the body of
    // a single-output `comb` is a bare expression, the value is implicitly
    // assigned to the single output. Detect: scope_statement has exactly
    // one named child and that child is an expression-as-statement node.
    bool   placeholder = false;
    TSNode expr_body{};
    if (kind == "comb" && output_refs.size() == 1 && std::string_view(ts_node_type(code)) == "scope_statement"
        && ts_node_named_child_count(code) == 1) {
      TSNode           only = ts_node_named_child(code, 0);
      std::string_view ot(ts_node_type(only));
      if (ot == "expression_item" || ot == "constant" || ot == "identifier" || ot == "unary_expression"
          || ot == "function_call_expression" || ot == "tuple" || ot == "tuple_sq" || ot == "if_expression"
          || ot == "match_expression" || ot == "bit_selection" || ot == "member_selection" || ot == "attribute_read"
          || ot == "dot_expression") {
        placeholder = true;
        expr_body   = only;
      }
    }
    if (placeholder) {
      Lnast_node val  = expr_to_node(expr_body);
      auto       aidx = builder.add_child(Lnast_ntype::create_assign());
      lnast->add_child(aidx, Lnast_node::create_ref(output_refs.front()));
      lnast->add_child(aidx, val);
    } else {
      process_scope_statement(code, body_idx);
    }
  }
  builder.pop_stmts();
}

void Prp2lnast::process_enum_assignment(TSNode n) {
  TSNode name = child_by_field(n, "name");
  if (ts_node_is_null(name)) {
    return;
  }
  Lnast_node ref = Lnast_node::create_ref(get_text(name));
  // LNAST ntype predates an `enum_add` primitive; reuse `tuple_add` plus an
  // `attr_set` marking the value as an enum.
  auto idx = builder.add_child(Lnast_ntype::create_tuple_add());
  lnast->add_child(idx, ref);
  auto aidx = builder.add_child(Lnast_ntype::create_attr_set());
  lnast->add_child(aidx, ref);
  lnast->add_child(aidx, Lnast_node::create_const("type"));
  lnast->add_child(aidx, Lnast_node::create_const("enum"));
}

void Prp2lnast::process_type_statement(TSNode n) {
  TSNode name = child_by_field(n, "name");
  if (ts_node_is_null(name)) {
    return;
  }
  Lnast_node ref = Lnast_node::create_ref(get_text(name));
  auto       idx = builder.add_child(Lnast_ntype::create_type_def());
  lnast->add_child(idx, ref);
  // Simplified: emit an unknown_type child for now.
  lnast->add_child(idx, Lnast_ntype::create_unknown_type());
}

void Prp2lnast::process_import_statement(TSNode n) {
  TSNode     alias  = child_by_field(n, "alias");
  TSNode     mod    = child_by_field(n, "module");
  Lnast_node target = ts_node_is_null(alias) ? builder.mint_tmp_ref() : Lnast_node::create_ref(get_text(alias));
  auto       idx    = builder.add_child(Lnast_ntype::create_func_call());
  lnast->add_child(idx, target);
  lnast->add_child(idx, Lnast_node::create_const("import"));
  lnast->add_child(idx, Lnast_node::create_const(ts_node_is_null(mod) ? std::string_view{} : get_text(mod)));
}

void Prp2lnast::process_test_statement(TSNode n) {
  TSNode code   = child_by_field(n, "code");
  auto   fd_idx = builder.add_child(Lnast_ntype::create_func_def());
  auto   tmp    = builder.mint_tmp_ref();
  lnast->add_child(fd_idx, tmp);
  lnast->add_child(fd_idx, Lnast_node::create_const("comb"));
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  auto body_idx = lnast->add_child(fd_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, body_idx);
  }
  builder.pop_stmts();
  auto aidx = builder.add_child(Lnast_ntype::create_attr_set());
  lnast->add_child(aidx, tmp);
  lnast->add_child(aidx, Lnast_node::create_const("test"));
  lnast->add_child(aidx, Lnast_node::create_const("true"));
}

void Prp2lnast::process_spawn_statement(TSNode n) {
  TSNode     name   = child_by_field(n, "name");
  Lnast_node ref    = ts_node_is_null(name) ? builder.mint_tmp_ref() : Lnast_node::create_ref(get_text(name));
  auto       fd_idx = builder.add_child(Lnast_ntype::create_func_def());
  lnast->add_child(fd_idx, ref);
  lnast->add_child(fd_idx, Lnast_node::create_const("comb"));
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_ntype::create_stmts());
  auto aidx = builder.add_child(Lnast_ntype::create_attr_set());
  lnast->add_child(aidx, ref);
  lnast->add_child(aidx, Lnast_node::create_const("spawn"));
  lnast->add_child(aidx, Lnast_node::create_const("true"));
}

void Prp2lnast::process_impl_statement(TSNode /*n*/) {
  // Simplified: emit as a placeholder assert to preserve node count.
  auto idx = builder.add_child(Lnast_ntype::create_cassert());
  lnast->add_child(idx, Lnast_node::create_const("true"));
}

// ---------------- Expressions ----------------

Lnast_node Prp2lnast::expr_to_node(TSNode n) {
  if (ts_node_is_null(n)) {
    return Lnast_node::create_const("0");
  }
  std::string_view t(ts_node_type(n));
  if (t == "identifier") {
    return identifier_to_node(n, /*for_lvalue=*/false);
  }
  if (t == "expression_item") {
    return binary_expr_to_node(n);
  }
  if (t == "constant") {
    // `constant` wraps one of: integer_literal, bool_literal, unknown_literal,
    // string_literal (single-quoted), or interpolated_string_literal (double-
    // quoted, may contain `{expr}` parts). Lower the inner literal directly
    // when it's a string variant — the interpolation/body handling lives in
    // expr_to_node's interpolated_string_literal branch. Other literals are
    // text-based; just emit the literal text.
    uint32_t nnc = ts_node_named_child_count(n);
    if (nnc == 1) {
      TSNode           c = ts_node_named_child(n, 0);
      std::string_view ct(ts_node_type(c));
      if (ct == "interpolated_string_literal") {
        return expr_to_node(c);
      }
    }
    return constant_text_to_node(trim(get_text(n)));
  }
  if (t == "integer_literal" || t == "bool_literal" || t == "unknown_literal" || t == "string_literal") {
    return constant_text_to_node(trim(get_text(n)));
  }
  if (t == "unary_expression") {
    return unary_expr_to_node(n);
  }
  if (t == "if_expression") {
    return if_expr_to_node(n);
  }
  if (t == "match_expression") {
    return match_expr_to_node(n);
  }
  if (t == "bit_selection") {
    return bit_selection_to_node(n);
  }
  if (t == "member_selection") {
    return member_selection_to_node(n);
  }
  if (t == "attribute_read") {
    return attribute_read_to_node(n);
  }
  if (t == "dot_expression") {
    return dot_expression_to_node(n);
  }
  if (t == "function_call_expression") {
    return function_call_expr_to_node(n);
  }
  if (t == "tuple") {
    return tuple_to_node(n, /*is_square=*/false);
  }
  if (t == "tuple_sq") {
    return tuple_to_node(n, /*is_square=*/true);
  }
  if (t == "typed_identifier") {
    TSNode id = child_by_field(n, "identifier");
    if (!ts_node_is_null(id)) {
      return identifier_to_node(id, false);
    }
  }
  if (t == "type_specification") {
    return type_specification_to_node(n);
  }
  if (t == "ref_identifier") {
    TSNode id = ts_node_named_child(n, 0);
    if (!ts_node_is_null(id)) {
      return identifier_to_node(id, false);
    }
  }
  if (t == "optional_expression") {
    TSNode arg = child_by_field(n, "argument");
    return expr_to_node(arg);
  }
  if (t == "scope_statement") {
    // Inline a scope expression: run its statements in the current scope
    // and return the last computed expression.
    process_scope_statement(n, builder.idx_stmts);
    return Lnast_node::create_const("0");
  }
  if (t == "interpolated_string_literal") {
    // Double-quoted string. When there are no `{expr}` interpolations, the
    // named-child list is empty (only the outer quote tokens, which are
    // anonymous), so re-emit the body as a single-quoted pyrope string
    // literal so `Lconst::from_pyrope` can round-trip it as a string.
    bool has_interp = ts_node_named_child_count(n) > 0;
    if (!has_interp) {
      auto body = text_between(ts_node_start_byte(n) + 1, ts_node_end_byte(n) - 1);
      return Lnast_node::create_const(absl::StrCat("'", body, "'"));
    }
    // Interpolated strings are not yet lowered; fall through to textual form.
  }
  // Fallback: treat as a constant from source text.
  return constant_text_to_node(trim(get_text(n)));
}

Lnast_node Prp2lnast::constant_text_to_node(std::string_view text) {
  if (text.empty()) {
    return Lnast_node::create_const("0");
  }
  return Lnast_node::create_const(std::string(text));
}

Lnast_node Prp2lnast::identifier_to_node(TSNode n, bool for_lvalue) {
  auto name = trim(get_text(n));
  // Keyword constants that fell through as identifiers.
  if (name == "true" || name == "false" || name == "nil") {
    return Lnast_node::create_const(name);
  }
  // Pyrope's `…` escape lets identifiers carry spaces/punctuation. When the
  // escaped text is a plain alnum/underscore identifier (the common case in
  // tests like ``a`` ↔ `a`), strip the backticks so the LNAST ref text is
  // the canonical name. The escape stays only when it actually carries a
  // special character.
  if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {
    auto inner = name.substr(1, name.size() - 2);
    bool ok    = !inner.empty();
    for (char ch : inner) {
      if (!(ch == '_' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) {
        ok = false;
        break;
      }
    }
    if (ok && !(inner[0] >= '0' && inner[0] <= '9')) {
      // `inner` is a substring view of `name` (same backing buffer), so the
      // string_view stays valid after rebind.
      name = inner;
    }
  }
  if (for_lvalue) {
    return Lnast_node::create_ref(name);
  }
  return Lnast_node::create_ref(name);
}

Lnast_node Prp2lnast::type_specification_to_node(TSNode n) {
  TSNode     arg  = child_by_field(n, "argument");
  Lnast_node aref = expr_to_node(arg);
  TSNode     ty   = child_by_field(n, "type");
  if (!ts_node_is_null(ty)) {
    auto idx = builder.add_child(Lnast_ntype::create_type_spec());
    lnast->add_child(idx, aref);
    emit_type_expr(idx, ty);
  }
  // The `attribute` field on `typedOrAttributed` wraps an anonymous
  // `seq(':', attribute_list)`, so `child_by_field("attribute")` returns
  // the `:` token rather than the attribute_list. Find the attribute_list
  // by walking children directly.
  uint32_t total = ts_node_child_count(n);
  for (uint32_t i = 0; i < total; i++) {
    TSNode c = ts_node_child(n, i);
    if (std::string_view(ts_node_type(c)) == "attribute_list") {
      emit_attribute_list(aref, c);
    }
  }
  return aref;
}

void Prp2lnast::emit_type_spec(const Lnast_node& target, TSNode type_cast_node) {
  // type_cast: ':' + (type + optional attribute | attribute).
  // Same field-vs-anonymous-seq quirk as `type_specification_to_node`: the
  // `attribute` field maps to a wrapping seq whose first child is `:`, so
  // walk children to find the actual attribute_list.
  TSNode ty = child_by_field(type_cast_node, "type");
  if (!ts_node_is_null(ty)) {
    // Tuple-shape type with default values (e.g. `:(x=0, y=1)`) is the only
    // way to declare a named-position tuple in Pyrope. Lower the inner
    // tuple as a normal `tuple_add` and assign it to the target so the
    // bundle starts with `:0:x=0, :1:y=1` keys; subsequent positional
    // assignments use upass's shape-preserving merge to keep those names.
    // The bit-width primitive types are still ignored at the value-folding
    // layer — this only handles the shape carried by the tuple type.
    std::string_view tt(ts_node_type(ty));
    if (tt == "expression_type") {
      uint32_t inner_count = ts_node_named_child_count(ty);
      for (uint32_t i = 0; i < inner_count; i++) {
        TSNode inner = ts_node_named_child(ty, i);
        if (std::string_view(ts_node_type(inner)) == "tuple") {
          auto bundle_ref = tuple_to_node(inner, false);
          auto aidx       = builder.add_child(Lnast_ntype::create_assign());
          lnast->add_child(aidx, target);
          lnast->add_child(aidx, bundle_ref);
          // Skip the empty `type_spec expr_type:` we'd otherwise emit; the
          // tuple-shape information is now encoded in the target's bundle.
          goto attrs;
        }
      }
    }
    {
      auto idx = builder.add_child(Lnast_ntype::create_type_spec());
      lnast->add_child(idx, target);
      emit_type_expr(idx, ty);
    }
  }
attrs:
  uint32_t total = ts_node_child_count(type_cast_node);
  for (uint32_t i = 0; i < total; i++) {
    TSNode c = ts_node_child(type_cast_node, i);
    if (std::string_view(ts_node_type(c)) == "attribute_list") {
      emit_attribute_list(target, c);
    }
  }
}

void Prp2lnast::emit_type_expr(const Lnast_nid& parent, TSNode type_node) {
  std::string_view t(ts_node_type(type_node));
  if (t == "uint_type" || t == "sint_type") {
    auto node = (t == "uint_type") ? Lnast_ntype::create_prim_type_uint() : Lnast_ntype::create_prim_type_sint();
    // Width (`u8`/`s16`/`i32`) embeds in the leading-letter+digits text. The
    // unsized spellings (`uint`, `unsigned`, `int`, `integer`) carry no digits;
    // emit the type marker without a width child.
    std::string txt(trim(get_text(type_node)));
    if (!txt.empty() && std::isdigit(static_cast<unsigned char>(txt.back()))) {
      // First char is u/s/i; the remainder is the bit width.
      auto idx = lnast->add_child(parent, node);
      lnast->add_child(idx, Lnast_node::create_const(txt.substr(1)));
    } else {
      lnast->add_child(parent, node);
    }
  } else if (t == "bool_type") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_boolean());
  } else if (t == "string_type") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_string());
  } else if (t == "array_type") {
    auto   idx  = lnast->add_child(parent, Lnast_ntype::create_comp_type_array());
    TSNode base = child_by_field(type_node, "base");
    if (!ts_node_is_null(base)) {
      emit_type_expr(idx, base);
    }
    TSNode len = child_by_field(type_node, "length");
    if (!ts_node_is_null(len)) {
      lnast->add_child(idx, expr_to_node(len));
    }
  } else if (t == "expression_type" || t == "dot_expression_type" || t == "function_call_type") {
    lnast->add_child(parent, Lnast_ntype::create_expr_type());
  } else {
    lnast->add_child(parent, Lnast_ntype::create_unknown_type());
  }
}

void Prp2lnast::emit_attribute_list(const Lnast_node& target, TSNode attr_list_node) {
  // attribute_list: '[' (name (= value)?)*  ']'.
  // The grammar's `field('item', seq(field('name', ...), field('value', ...)))`
  // wraps an anonymous seq, so each item's inner field names appear directly
  // on the attribute_list. Walk children in source order and pair each
  // `name` with the immediately-following `value` field (if any).
  uint32_t total = ts_node_child_count(attr_list_node);
  for (uint32_t i = 0; i < total; i++) {
    const char* fname = ts_node_field_name_for_child(attr_list_node, i);
    if (!fname || std::string_view(fname) != "name") {
      continue;
    }
    TSNode name_n = ts_node_child(attr_list_node, i);
    // Look ahead for an optional `value` field that pairs with this name.
    // Stop at the next `name` field — that begins a new item. The grammar
    // wraps the value in an anonymous `seq('=', expr)` and applies
    // `field('value', ...)` to the seq, so the field tag covers BOTH the
    // `=` token and the expression child. Skip the `=` token and pick the
    // first named child carrying the `value` field.
    TSNode val_n;
    bool   have_val = false;
    for (uint32_t k = i + 1; k < total; k++) {
      const char* knm = ts_node_field_name_for_child(attr_list_node, k);
      if (!knm) {
        continue;
      }
      std::string_view knms(knm);
      if (knms == "name") {
        break;
      }
      if (knms == "value") {
        TSNode kc = ts_node_child(attr_list_node, k);
        if (!ts_node_is_named(kc)) {
          continue;  // `=` token; the real value is the next field-tagged child
        }
        val_n    = kc;
        have_val = true;
        break;
      }
    }
    auto idx = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(idx, target);
    lnast->add_child(idx, Lnast_node::create_const(get_text(name_n)));
    if (have_val) {
      lnast->add_child(idx, expr_to_node(val_n));
    } else {
      lnast->add_child(idx, Lnast_node::create_const("true"));
    }
  }
}

// Lower an `expression_item` (flat operand/operator chain, one per grammar
// priority tier). Four operator classes:
//
//   binary_times_op   *  /  %                                   (tier 2)
//   binary_other_op   +  -  ++  <<  >>  &  |  ^  !&  !|  !^
//                     ..=  ..<  ..+  step                       (tier 3)
//   binary_compare_op <  <=  >  >=  ==  !=
//                     has !has  in !in  case !case  does !does
//                     is  !is   equals !equals                  (tier 4)
//   binary_logical_op and !and  or !or  implies !implies        (tier 5)
//
// Compare chains (`a == b == c`) lower as Python-style
// `(a op0 b) and (b op1 c)` — one `expression_item` may hold multiple
// compare operators. Logical chains left-fold, but mixing distinct
// logical operators (`a and b or c`) is an ambiguity we flag rather than
// silently pick an associativity.
Lnast_node Prp2lnast::binary_expr_to_node(TSNode n) {
  uint32_t nnc = ts_node_named_child_count(n);
  if (nnc == 0) {
    return builder.mint_tmp_ref();
  }

  // Tier of a binary_*_op wrapper. Drives chaining behavior (compare
  // chains lower as Python-style `(a op b) and (b op c)`; the rest left-fold).
  enum class Tier : uint8_t { times, other, compare, logical, unknown };

  // Collect operands and operator kinds in source order. Each operator is the
  // inner aliased child of the binary_*_op wrapper (e.g. `op_add`, `op_eq`).
  // We carry both the inner kind (for op-specific dispatch) and the wrapper's
  // tier (for chain-vs-fold dispatch).
  struct Op {
    Tier             tier = Tier::unknown;
    std::string_view kind;  // points into ts_node_type's static string for the inner aliased op
  };
  std::vector<Lnast_node> operands;
  std::vector<Op>         ops;
  operands.reserve(nnc / 2 + 1);
  ops.reserve(nnc / 2);
  for (uint32_t i = 0; i < nnc; i++) {
    TSNode           c = ts_node_named_child(n, i);
    std::string_view ct(ts_node_type(c));
    Tier             tier = Tier::unknown;
    if (ct == "binary_times_op") {
      tier = Tier::times;
    } else if (ct == "binary_other_op") {
      tier = Tier::other;
    } else if (ct == "binary_compare_op") {
      tier = Tier::compare;
    } else if (ct == "binary_logical_op") {
      tier = Tier::logical;
    }
    if (tier != Tier::unknown) {
      // The wrapper has a single named child of the aliased op kind.
      TSNode inner = ts_node_named_child(c, 0);
      ops.push_back({tier, ts_node_is_null(inner) ? std::string_view{} : std::string_view(ts_node_type(inner))});
    } else {
      operands.push_back(expr_to_node(c));
    }
  }

  if (ops.empty()) {
    return operands.front();
  }
  I(operands.size() == ops.size() + 1);

  // Emit `head(ref(tmp), l, r)` and return the tmp ref. Used both for plain
  // binary ops (eq/lt/plus/...) and for typed pseudo-calls (func_has, func_in)
  // — the shape is identical.
  auto make_binop = [&](Lnast_ntype::Lnast_ntype_int head, const Lnast_node& l, const Lnast_node& r) {
    auto idx = builder.add_child(head);
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, l);
    lnast->add_child(idx, r);
    return ref;
  };
  // Legacy marker-style call: `func_call(ref(tmp), const(name), l, r)` —
  // still used by ops without a dedicated LNAST ntype (step / implies).
  auto make_call = [&](const char* name, const Lnast_node& l, const Lnast_node& r) {
    auto idx = builder.add_child(Lnast_ntype::create_func_call());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, Lnast_node::create_const(name));
    lnast->add_child(idx, l);
    lnast->add_child(idx, r);
    return ref;
  };
  auto wrap_not = [&](const Lnast_node& inner) {
    auto idx = builder.add_child(Lnast_ntype::create_log_not());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, inner);
    return ref;
  };

  auto emit_compare = [&](std::string_view kind, const Lnast_node& l, const Lnast_node& r) -> Lnast_node {
    if (kind == "op_eq") {
      return make_binop(Lnast_ntype::create_eq(), l, r);
    }
    if (kind == "op_ne") {
      return make_binop(Lnast_ntype::create_ne(), l, r);
    }
    if (kind == "op_lt") {
      return make_binop(Lnast_ntype::create_lt(), l, r);
    }
    if (kind == "op_le") {
      return make_binop(Lnast_ntype::create_le(), l, r);
    }
    if (kind == "op_gt") {
      return make_binop(Lnast_ntype::create_gt(), l, r);
    }
    if (kind == "op_ge") {
      return make_binop(Lnast_ntype::create_ge(), l, r);
    }
    if (kind == "op_has") {
      return make_binop(Lnast_ntype::create_func_has(), l, r);
    }
    if (kind == "op_not_has") {
      return wrap_not(make_binop(Lnast_ntype::create_func_has(), l, r));
    }
    if (kind == "op_in") {
      return make_binop(Lnast_ntype::create_func_in(), l, r);
    }
    if (kind == "op_not_in") {
      return wrap_not(make_binop(Lnast_ntype::create_func_in(), l, r));
    }
    if (kind == "op_is") {
      return make_binop(Lnast_ntype::create_is(), l, r);
    }
    if (kind == "op_not_is") {
      return wrap_not(make_binop(Lnast_ntype::create_is(), l, r));
    }
    if (kind == "op_does") {
      return make_binop(Lnast_ntype::create_func_does(), l, r);
    }
    if (kind == "op_not_does") {
      return wrap_not(make_binop(Lnast_ntype::create_func_does(), l, r));
    }
    if (kind == "op_case") {
      return make_binop(Lnast_ntype::create_func_case(), l, r);
    }
    if (kind == "op_not_case") {
      return wrap_not(make_binop(Lnast_ntype::create_func_case(), l, r));
    }
    if (kind == "op_equals") {
      return make_binop(Lnast_ntype::create_func_equals(), l, r);
    }
    if (kind == "op_not_equals") {
      return wrap_not(make_binop(Lnast_ntype::create_func_equals(), l, r));
    }
    std::print("prp2lnast: unhandled compare op `{}`\n", kind);
    return builder.mint_tmp_ref();
  };

  auto emit_other = [&](std::string_view kind, const Lnast_node& l, const Lnast_node& r) -> Lnast_node {
    if (kind == "op_add") {
      return make_binop(Lnast_ntype::create_plus(), l, r);
    }
    if (kind == "op_sub") {
      return make_binop(Lnast_ntype::create_minus(), l, r);
    }
    if (kind == "op_tuple_concat") {
      return make_binop(Lnast_ntype::create_tuple_concat(), l, r);
    }
    if (kind == "op_shl") {
      return make_binop(Lnast_ntype::create_shl(), l, r);
    }
    if (kind == "op_sra") {
      return make_binop(Lnast_ntype::create_sra(), l, r);
    }
    if (kind == "op_bit_and") {
      return make_binop(Lnast_ntype::create_bit_and(), l, r);
    }
    if (kind == "op_bit_or") {
      return make_binop(Lnast_ntype::create_bit_or(), l, r);
    }
    if (kind == "op_bit_xor") {
      return make_binop(Lnast_ntype::create_bit_xor(), l, r);
    }
    if (kind == "op_bit_nand") {
      return wrap_not(make_binop(Lnast_ntype::create_bit_and(), l, r));
    }
    if (kind == "op_bit_nor") {
      return wrap_not(make_binop(Lnast_ntype::create_bit_or(), l, r));
    }
    if (kind == "op_bit_xnor") {
      return wrap_not(make_binop(Lnast_ntype::create_bit_xor(), l, r));
    }
    if (kind == "op_range_inclusive") {
      return make_binop(Lnast_ntype::create_range(), l, r);
    }
    if (kind == "op_range_exclusive") {
      auto m    = builder.add_child(Lnast_ntype::create_minus());
      auto mref = builder.mint_tmp_ref();
      lnast->add_child(m, mref);
      lnast->add_child(m, r);
      lnast->add_child(m, Lnast_node::create_const("1"));
      return make_binop(Lnast_ntype::create_range(), l, mref);
    }
    if (kind == "op_range_count") {
      auto p    = builder.add_child(Lnast_ntype::create_plus());
      auto pref = builder.mint_tmp_ref();
      lnast->add_child(p, pref);
      lnast->add_child(p, l);
      lnast->add_child(p, r);
      auto mm   = builder.add_child(Lnast_ntype::create_minus());
      auto mref = builder.mint_tmp_ref();
      lnast->add_child(mm, mref);
      lnast->add_child(mm, pref);
      lnast->add_child(mm, Lnast_node::create_const("1"));
      return make_binop(Lnast_ntype::create_range(), l, mref);
    }
    // `step` has no dedicated LNAST op; lower as a two-arg call for now.
    if (kind == "op_step") {
      return make_call("step", l, r);
    }
    std::print("prp2lnast: unhandled `binary_other_op` `{}`\n", kind);
    return builder.mint_tmp_ref();
  };

  auto emit_times = [&](std::string_view kind, const Lnast_node& l, const Lnast_node& r) -> Lnast_node {
    if (kind == "op_mul") {
      return make_binop(Lnast_ntype::create_mult(), l, r);
    }
    if (kind == "op_div") {
      return make_binop(Lnast_ntype::create_div(), l, r);
    }
    if (kind == "op_mod") {
      return make_binop(Lnast_ntype::create_mod(), l, r);
    }
    std::print("prp2lnast: unhandled `binary_times_op` `{}`\n", kind);
    return builder.mint_tmp_ref();
  };

  auto emit_logical = [&](std::string_view kind, const Lnast_node& l, const Lnast_node& r) -> Lnast_node {
    if (kind == "op_log_and") {
      return make_binop(Lnast_ntype::create_log_and(), l, r);
    }
    if (kind == "op_log_or") {
      return make_binop(Lnast_ntype::create_log_or(), l, r);
    }
    if (kind == "op_log_nand") {
      return wrap_not(make_binop(Lnast_ntype::create_log_and(), l, r));
    }
    if (kind == "op_log_nor") {
      return wrap_not(make_binop(Lnast_ntype::create_log_or(), l, r));
    }
    if (kind == "op_implies") {
      return make_call("implies", l, r);
    }
    if (kind == "op_not_implies") {
      return wrap_not(make_call("implies", l, r));
    }
    std::print("prp2lnast: unhandled `binary_logical_op` `{}`\n", kind);
    return builder.mint_tmp_ref();
  };

  const Tier first_tier = ops.front().tier;

  // Compare tier: Python-style chain. For each adjacent pair emit the
  // corresponding compare; then left-fold `log_and` over the results.
  if (first_tier == Tier::compare) {
    std::vector<Lnast_node> pair_results;
    pair_results.reserve(ops.size());
    for (size_t i = 0; i < ops.size(); ++i) {
      pair_results.push_back(emit_compare(ops[i].kind, operands[i], operands[i + 1]));
    }
    Lnast_node acc = pair_results.front();
    for (size_t i = 1; i < pair_results.size(); ++i) {
      acc = make_binop(Lnast_ntype::create_log_and(), acc, pair_results[i]);
    }
    return acc;
  }

  // Logical tier: mixing distinct operators (`a and b or c`) is ambiguous —
  // same precedence, different semantics. Require explicit parens.
  // TODO(upass.md): turn this into a proper diagnostic with source range
  // once the pass gains a diagnostics channel.
  if (first_tier == Tier::logical) {
    for (size_t i = 1; i < ops.size(); ++i) {
      if (ops[i].kind != ops[0].kind) {
        std::print(
            "prp2lnast: mixed `{}` and `{}` at the same precedence — add "
            "parentheses to disambiguate\n",
            ops[0].kind,
            ops[i].kind);
        break;
      }
    }
  }

  // All other tiers (and logical after the ambiguity warning): left-fold.
  Lnast_node acc = operands.front();
  for (size_t i = 0; i < ops.size(); ++i) {
    switch (ops[i].tier) {
      case Tier::times: acc = emit_times(ops[i].kind, acc, operands[i + 1]); break;
      case Tier::other: acc = emit_other(ops[i].kind, acc, operands[i + 1]); break;
      case Tier::logical: acc = emit_logical(ops[i].kind, acc, operands[i + 1]); break;
      default: std::print("prp2lnast: unknown op kind `{}`\n", ops[i].kind); return builder.mint_tmp_ref();
    }
  }
  return acc;
}

Lnast_node Prp2lnast::unary_expr_to_node(TSNode n) {
  TSNode     op_n  = child_by_field(n, "operator");
  TSNode     arg_n = child_by_field(n, "argument");
  Lnast_node arg_ref;
  if (ts_node_is_null(arg_n)) {
    auto start = ts_node_is_null(op_n) ? ts_node_start_byte(n) : ts_node_end_byte(op_n);
    arg_ref    = constant_text_to_node(trim(text_between(start, ts_node_end_byte(n))));
  } else {
    arg_ref = expr_to_node(arg_n);
  }
  // The `operator` field is one of the aliased operator nodes:
  // op_log_not / op_bit_not / op_unary_minus / op_spread.
  std::string_view op_kind = ts_node_is_null(op_n) ? std::string_view{} : std::string_view(ts_node_type(op_n));
  if (op_kind == "op_log_not") {
    auto idx = builder.add_child(Lnast_ntype::create_log_not());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, arg_ref);
    return ref;
  }
  if (op_kind == "op_bit_not") {
    auto idx = builder.add_child(Lnast_ntype::create_bit_not());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, arg_ref);
    return ref;
  }
  if (op_kind == "op_unary_minus") {
    auto idx = builder.add_child(Lnast_ntype::create_minus());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, Lnast_node::create_const("0"));
    lnast->add_child(idx, arg_ref);
    return ref;
  }
  // op_spread: pass-through for now.
  return arg_ref;
}

Lnast_node Prp2lnast::if_expr_to_node(TSNode n) {
  // if init?; cond { code } (elif init?; cond { code })* (else { code })?
  //
  // Order matters: we must evaluate every condition expression *before*
  // adding the `if` LNAST node to builder.idx_stmts. expr_to_node can emit
  // helper statements (e.g. `ne ___2 = i != 2`) into builder.idx_stmts; if the
  // `if` is already there, those helpers land as siblings *after* the
  // `if`, and the resulting LNAST has the `if` reading a tmp ref before
  // the producing ne — a downstream constprop pass that drops the
  // already-folded ne leaves the if's ref dangling. lnastfmt's
  // read-without-write check flags this. By staging cond evaluation first,
  // every helper stmt lands before the `if` and the consumer-after-producer
  // invariant holds.
  //
  // Caveat: this evaluates ALL elif conds eagerly into the surrounding
  // scope, even those that strictly should only run if the prior arm
  // missed. For pyrope comptime cond expressions this is fine (no side
  // effects); a future change could lower elif-conds inside their parent
  // arm's else branch for correctness on side-effecting conds.
  auto result = builder.mint_tmp_ref();

  struct Arm {
    Lnast_node cref;
    TSNode     code;
  };
  std::vector<Arm> arms;
  TSNode           else_code{};
  bool             have_else = false;
  bool             init_done = false;

  auto eval_init = [&]() {
    if (init_done) {
      return;
    }
    init_done   = true;
    TSNode init = child_by_field(n, "init");
    if (!ts_node_is_null(init)) {
      uint32_t inc = ts_node_named_child_count(init);
      for (uint32_t i = 0; i < inc; i++) {
        process_statement(ts_node_named_child(init, i));
      }
    }
  };

  uint32_t nc = child_count(n);
  TSNode   pending_cond{};
  bool     have_cond = false;
  bool     in_else   = false;
  for (uint32_t i = 0; i < nc; i++) {
    const char* field_name = ts_node_field_name_for_child(n, i);
    if (!field_name) {
      continue;
    }
    std::string_view f(field_name);
    TSNode           c = child(n, i);
    if (f == "condition") {
      pending_cond = c;
      have_cond    = true;
    } else if (f == "code") {
      if (in_else) {
        else_code = c;
        have_else = true;
        in_else   = false;
      } else if (have_cond) {
        eval_init();
        Lnast_node cref = ts_node_is_null(pending_cond) ? Lnast_node::create_const("true") : expr_to_node(pending_cond);
        arms.push_back({cref, c});
        have_cond = false;
      }
    } else if (f == "else") {
      // Tree-sitter applies field("else", optseq("else", scope_statement))
      // by tagging EVERY child of the optseq with field "else" — so we see
      // two "else"-tagged children: the literal keyword and the body. The
      // keyword has node type "else" (text "else"); the body has type
      // "scope_statement". Capture the latter as the else body.
      std::string_view ct(ts_node_type(c));
      if (ct == "scope_statement") {
        else_code = c;
        have_else = true;
        in_else   = false;
      } else {
        std::string_view txt = get_text(c);
        if (txt == "else") {
          in_else = true;
        }
      }
    }
  }

  // All cond stmts (and init) have now been emitted to builder.idx_stmts. Add the
  // `if` here so it follows its producers in source order.
  auto if_idx = builder.add_child(Lnast_ntype::create_if());

  auto emit_body = [&](TSNode code) {
    auto body_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
    builder.push_stmts(body_idx);
    if (!ts_node_is_null(code)) {
      process_scope_statement(code, body_idx);
    }
    auto a = builder.add_child(Lnast_ntype::create_assign());
    lnast->add_child(a, result);
    lnast->add_child(a, Lnast_node::create_const("0"));
    builder.pop_stmts();
  };

  for (const auto& arm : arms) {
    lnast->add_child(if_idx, arm.cref);
    emit_body(arm.code);
  }
  if (have_else) {
    emit_body(else_code);
  }

  return result;
}

Lnast_node Prp2lnast::match_expr_to_node(TSNode n) {
  // match init?; subject { (operator? expr_list | else) { code } ... }
  // Simplified: produce a uif chain with equality checks.
  TSNode subject   = {};
  TSNode else_code = {};
  bool   have_subj = false;
  // Find subject: the first 'condition' field child is the subject.
  for (uint32_t i = 0; i < child_count(n); i++) {
    const char* f = ts_node_field_name_for_child(n, i);
    if (!f) {
      continue;
    }
    if (std::string_view(f) == "condition") {
      subject   = child(n, i);
      have_subj = true;
      break;
    }
  }
  if (!have_subj) {
    return Lnast_node::create_const("0");
  }
  Lnast_node subject_ref = expr_to_node(subject);

  // Same producer-before-consumer reordering as if_expr_to_node: walk every
  // arm first, emit each arm's eq/log_or compute stmts to builder.idx_stmts,
  // collect (cref, code) pairs, THEN add the if and assemble. Otherwise
  // every eq lands as a sibling AFTER the if and the if's cond refs are
  // dangling once constprop drops the eqs.
  Lnast_node result = builder.mint_tmp_ref();

  struct Arm {
    Lnast_node cref;
    TSNode     code;
  };
  std::vector<Arm> arms;

  bool   saw_first_condition = false;
  TSNode pending_expr_list   = {};
  bool   have_pending        = false;
  bool   pending_is_else     = false;
  for (uint32_t i = 0; i < child_count(n); i++) {
    const char* f = ts_node_field_name_for_child(n, i);
    if (!f) {
      continue;
    }
    std::string_view field(f);
    TSNode           c = child(n, i);
    if (field == "condition") {
      if (!saw_first_condition) {
        saw_first_condition = true;
        continue;
      }
      std::string_view t(ts_node_type(c));
      std::string_view txt = trim(get_text(c));
      if (txt == "else") {
        pending_is_else = true;
      } else if (t == "expression_list") {
        pending_expr_list = c;
        have_pending      = true;
      }
    } else if (field == "code") {
      if (pending_is_else) {
        else_code       = c;
        pending_is_else = false;
      } else if (have_pending) {
        // Emit `eq tmp = subject_ref rhs` and return the tmp ref.
        auto emit_eq = [&](const Lnast_node& rhs) {
          auto idx = builder.add_child(Lnast_ntype::create_eq());
          auto ref = builder.mint_tmp_ref();
          lnast->add_child(idx, ref);
          lnast->add_child(idx, subject_ref);
          lnast->add_child(idx, rhs);
          return ref;
        };

        Lnast_node arm_cond;
        uint32_t   nnc = ts_node_named_child_count(pending_expr_list);
        if (nnc == 0) {
          arm_cond = emit_eq(constant_text_to_node(trim(get_text(pending_expr_list))));
        } else if (nnc == 1) {
          arm_cond = emit_eq(expr_to_node(ts_node_named_child(pending_expr_list, 0)));
        } else {
          auto or_idx = builder.add_child(Lnast_ntype::create_log_or());
          arm_cond    = builder.mint_tmp_ref();
          lnast->add_child(or_idx, arm_cond);
          for (uint32_t j = 0; j < nnc; j++) {
            lnast->add_child(or_idx, emit_eq(expr_to_node(ts_node_named_child(pending_expr_list, j))));
          }
        }
        arms.push_back({arm_cond, c});
        have_pending = false;
      }
    }
  }

  // All eq/log_or compute stmts have been emitted to builder.idx_stmts. Add the
  // `if` here so it follows its producers in source order.
  auto if_idx = builder.add_child(Lnast_ntype::create_if());

  auto emit_body = [&](TSNode code) {
    auto body_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
    builder.push_stmts(body_idx);
    process_scope_statement(code, body_idx);
    auto a = builder.add_child(Lnast_ntype::create_assign());
    lnast->add_child(a, result);
    lnast->add_child(a, Lnast_node::create_const("0"));
    builder.pop_stmts();
  };

  for (const auto& arm : arms) {
    lnast->add_child(if_idx, arm.cref);
    emit_body(arm.code);
  }
  if (else_code.tree != nullptr) {
    emit_body(else_code);
  }
  return result;
}

Lnast_node Prp2lnast::bit_selection_to_node(TSNode n) {
  // bit_selection: argument '#' [reduction] select
  // LNAST get_mask expects a bitmask, not the selected bit position/range.
  TSNode arg = child_by_field(n, "argument");
  if (ts_node_is_null(arg)) {
    arg = ts_node_named_child(n, 0);
  }
  Lnast_node base = expr_to_node(arg);

  // The `select` field is `multiple: true` (covers the `#` token, optional
  // reduction, and the inner select node) — `child_by_field` returns the
  // first match (the `#` token). Walk children for the actual select node.
  TSNode sel_node{};
  for (uint32_t i = 0; i < child_count(n); i++) {
    TSNode           c = child(n, i);
    std::string_view ct(ts_node_type(c));
    if (ct == "select") {
      sel_node = c;
      break;
    }
  }
  TSNode type_node = child_by_field(n, "reduction");

  auto make_const_mask = [](std::string_view text) -> std::optional<Lnast_node> {
    auto v = Lconst::from_pyrope(text);
    if (v.is_invalid() || !v.is_i()) {
      return std::nullopt;
    }
    return Lnast_node::create_const(Lconst::get_mask_value(static_cast<Bits_t>(v.to_i())).to_pyrope());
  };

  auto make_range_mask = [&](TSNode expr_item) -> std::optional<Lnast_node> {
    if (ts_node_named_child_count(expr_item) != 3) {
      return std::nullopt;
    }
    TSNode lo = ts_node_named_child(expr_item, 0);
    TSNode op = ts_node_named_child(expr_item, 1);
    TSNode hi = ts_node_named_child(expr_item, 2);
    // Inclusive ranges only — `..=`. The aliased child of the binary_other_op
    // wrapper names the operator without text.
    if (std::string_view(ts_node_type(op)) != "binary_other_op") {
      return std::nullopt;
    }
    TSNode op_inner = ts_node_named_child(op, 0);
    if (ts_node_is_null(op_inner) || std::string_view(ts_node_type(op_inner)) != "op_range_inclusive") {
      return std::nullopt;
    }

    auto lo_v = Lconst::from_pyrope(get_text(lo));
    auto hi_v = Lconst::from_pyrope(get_text(hi));
    if (lo_v.is_invalid() || hi_v.is_invalid() || !lo_v.is_i() || !hi_v.is_i()) {
      return std::nullopt;
    }

    return Lnast_node::create_const(
        Lconst::get_mask_value(static_cast<Bits_t>(hi_v.to_i()), static_cast<Bits_t>(lo_v.to_i())).to_pyrope());
  };

  auto make_dynamic_mask = [&](TSNode expr) {
    auto pos = expr_to_node(expr);
    auto idx = builder.add_child(Lnast_ntype::create_shl());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, Lnast_node::create_const("1"));
    lnast->add_child(idx, pos);
    return ref;
  };

  // Helper: emit a `range` LNAST node (start, end) and return its ref.
  // Used by the open-end and from-zero forms below; constprop's
  // process_get_mask consults range_map to synthesize the mask integer
  // (closed `lo..=hi` → bits-lo..hi mask; open `lo..` → -(1<<lo)).
  auto emit_range = [&](const Lnast_node& start, const Lnast_node& end) {
    auto rng_idx = builder.add_child(Lnast_ntype::create_range());
    auto rng_ref = builder.mint_tmp_ref();
    lnast->add_child(rng_idx, rng_ref);
    lnast->add_child(rng_idx, start);
    lnast->add_child(rng_idx, end);
    return rng_ref;
  };

  Lnast_node mask_ref = Lnast_node::create_const("-1");
  if (!ts_node_is_null(sel_node)) {
    TSNode list  = child_by_field(sel_node, "list");
    TSNode range = child_by_field(sel_node, "range");

    if (!ts_node_is_null(range)) {
      // selection_range carries one of:
      //   open_all              — bare `..`
      //   open_from: <expr>     — `expr..`
      //   from_zero_inclusive   — `..=expr`
      //   from_zero_exclusive   — `..<expr`
      TSNode open_all  = ts_node_child_by_field_name(range, "open_all", 8);
      TSNode open_from = ts_node_child_by_field_name(range, "open_from", 9);
      TSNode fz_incl   = ts_node_child_by_field_name(range, "from_zero_inclusive", 19);
      TSNode fz_excl   = ts_node_child_by_field_name(range, "from_zero_exclusive", 19);
      if (!ts_node_is_null(open_all)) {
        mask_ref = emit_range(Lnast_node::create_const("0"), Lnast_node::create_const("nil"));
      } else if (!ts_node_is_null(open_from)) {
        mask_ref = emit_range(expr_to_node(open_from), Lnast_node::create_const("nil"));
      } else if (!ts_node_is_null(fz_incl) || !ts_node_is_null(fz_excl)) {
        bool       is_lt    = !ts_node_is_null(fz_excl);
        TSNode     expr_n   = is_lt ? fz_excl : fz_incl;
        Lnast_node end_expr = expr_to_node(expr_n);
        Lnast_node end_node = end_expr;
        if (is_lt) {
          // `..<n` is exclusive: end = n - 1 (matches the inclusive form
          // stored on the range node downstream).
          auto m    = builder.add_child(Lnast_ntype::create_minus());
          auto mref = builder.mint_tmp_ref();
          lnast->add_child(m, mref);
          lnast->add_child(m, end_expr);
          lnast->add_child(m, Lnast_node::create_const("1"));
          end_node = mref;
        }
        mask_ref = emit_range(Lnast_node::create_const("0"), end_node);
      }
    } else if (!ts_node_is_null(list) && ts_node_named_child_count(list) > 0) {
      TSNode expr = ts_node_named_child(list, 0);
      auto   et   = std::string_view(ts_node_type(expr));
      if (et == "expression_item") {
        if (auto const_mask = make_range_mask(expr)) {
          mask_ref = *const_mask;
        } else {
          mask_ref = expr_to_node(expr);
        }
      } else {
        if (auto const_mask = make_const_mask(get_text(expr))) {
          mask_ref = *const_mask;
        } else {
          mask_ref = make_dynamic_mask(expr);
        }
      }
    }
  } else {
    Pass::error("missing select node in bit selection `{}`", get_text(n));
  }

  auto idx = builder.add_child(Lnast_ntype::create_get_mask());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, base);
  lnast->add_child(idx, mask_ref);

  if (!ts_node_is_null(type_node)) {
    std::string_view rt(ts_node_type(type_node));
    auto             red_node = Lnast_ntype::create_invalid();
    if (rt == "reduction_or") {
      red_node = Lnast_ntype::create_red_or();
    } else if (rt == "reduction_and") {
      red_node = Lnast_ntype::create_red_and();
    } else if (rt == "reduction_xor") {
      red_node = Lnast_ntype::create_red_xor();
    } else if (rt == "reduction_popcount") {
      red_node = Lnast_ntype::create_popcount();
    } else {
      // sign_extend / zero_extend not yet lowered.
      return ref;
    }
    auto r_idx = builder.add_child(red_node);
    auto r_ref = builder.mint_tmp_ref();
    lnast->add_child(r_idx, r_ref);
    lnast->add_child(r_idx, ref);
    return r_ref;
  }
  return ref;
}

Lnast_node Prp2lnast::member_selection_to_node(TSNode n) {
  TSNode     arg  = child_by_field(n, "argument");
  Lnast_node base = expr_to_node(arg);
  // Collect field nodes first — each may emit auxiliary stmts (range, minus)
  // that must precede the tuple_get so single-pass constprop can resolve the
  // field's value before consuming it. Adding the tuple_get last keeps the
  // dependency order without a post-hoc reordering pass.
  auto emit_range = [&](const Lnast_node& start, const Lnast_node& end) {
    auto rng_idx = builder.add_child(Lnast_ntype::create_range());
    auto rng_ref = builder.mint_tmp_ref();
    lnast->add_child(rng_idx, rng_ref);
    lnast->add_child(rng_idx, start);
    lnast->add_child(rng_idx, end);
    return rng_ref;
  };
  std::vector<Lnast_node> fields;
  uint32_t                nnc = ts_node_named_child_count(n);
  for (uint32_t i = 1; i < nnc; i++) {
    TSNode sel = ts_node_named_child(n, i);

    TSNode list = child_by_field(sel, "list");
    if (!ts_node_is_null(list)) {
      uint32_t lnnc = ts_node_named_child_count(list);
      for (uint32_t j = 0; j < lnnc; j++) {
        fields.push_back(expr_to_node(ts_node_named_child(list, j)));
      }
      continue;
    }

    TSNode range = child_by_field(sel, "range");
    if (!ts_node_is_null(range)) {
      TSNode open_all  = ts_node_child_by_field_name(range, "open_all", 8);
      TSNode open_from = ts_node_child_by_field_name(range, "open_from", 9);
      TSNode fz_incl   = ts_node_child_by_field_name(range, "from_zero_inclusive", 19);
      TSNode fz_excl   = ts_node_child_by_field_name(range, "from_zero_exclusive", 19);
      // `nil` is the open-end sentinel — round-trips through Lconst as a
      // string and is recognised by process_tuple_get as "slice to source's
      // last index". Using a real value here would falsely truncate.
      if (!ts_node_is_null(open_all)) {
        fields.push_back(emit_range(Lnast_node::create_const("0"), Lnast_node::create_const("nil")));
      } else if (!ts_node_is_null(open_from)) {
        fields.push_back(emit_range(expr_to_node(open_from), Lnast_node::create_const("nil")));
      } else if (!ts_node_is_null(fz_incl) || !ts_node_is_null(fz_excl)) {
        bool       is_lt    = !ts_node_is_null(fz_excl);
        TSNode     fz_n     = is_lt ? fz_excl : fz_incl;
        Lnast_node end_expr = expr_to_node(fz_n);
        Lnast_node end_node = end_expr;
        if (is_lt) {
          // `..<n` is exclusive: end = n - 1 to match the inclusive form
          // stored on the range node.
          auto m    = builder.add_child(Lnast_ntype::create_minus());
          auto mref = builder.mint_tmp_ref();
          lnast->add_child(m, mref);
          lnast->add_child(m, end_expr);
          lnast->add_child(m, Lnast_node::create_const("1"));
          end_node = mref;
        }
        fields.push_back(emit_range(Lnast_node::create_const("0"), end_node));
      }
      continue;
    }
  }
  auto idx = builder.add_child(Lnast_ntype::create_tuple_get());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, base);
  for (const auto& f : fields) {
    lnast->add_child(idx, f);
  }
  return ref;
}

Lnast_node Prp2lnast::attribute_read_to_node(TSNode n) {
  // attribute_read: argument '.' attribute_list (one or more chained reads)
  // Example: `x.[bits]` reads attribute `bits` from `x`. Lower as a sequence
  // of `attr_get` ops. Multiple chained reads (`x.[a].[b]`) chain through a
  // temporary.
  TSNode     arg  = child_by_field(n, "argument");
  Lnast_node base = expr_to_node(arg);

  uint32_t nnc = ts_node_named_child_count(n);
  for (uint32_t i = 1; i < nnc; i++) {
    TSNode al = ts_node_named_child(n, i);
    if (std::string_view(ts_node_type(al)) != "attribute_list") {
      continue;
    }
    // attribute_list children carry inner `name`/`value` fields directly
    // (the grammar wraps each item in an anonymous seq, so the field tags
    // appear on attribute_list itself). Reads only use the names; any
    // `=value` on a read is overparse and is silently dropped here.
    uint32_t total = ts_node_child_count(al);
    for (uint32_t j = 0; j < total; j++) {
      const char* fname = ts_node_field_name_for_child(al, j);
      if (!fname || std::string_view(fname) != "name") {
        continue;
      }
      TSNode name_n = ts_node_child(al, j);
      auto   idx    = builder.add_child(Lnast_ntype::create_attr_get());
      auto   ref    = builder.mint_tmp_ref();
      lnast->add_child(idx, ref);
      lnast->add_child(idx, base);
      lnast->add_child(idx, Lnast_node::create_const(get_text(name_n)));
      base = ref;
    }
  }
  return base;
}

Lnast_node Prp2lnast::dot_expression_to_node(TSNode n) {
  // dot_expression: item ('.' identifier|const)+
  uint32_t nnc = ts_node_named_child_count(n);
  if (nnc == 0) {
    return Lnast_node::create_ref(trim(get_text(n)));
  }
  Lnast_node base = expr_to_node(ts_node_named_child(n, 0));
  auto       idx  = builder.add_child(Lnast_ntype::create_tuple_get());
  auto       ref  = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, base);
  for (uint32_t i = 1; i < nnc; i++) {
    TSNode c = ts_node_named_child(n, i);
    lnast->add_child(idx, Lnast_node::create_const(trim(get_text(c))));
  }
  return ref;
}

Lnast_node Prp2lnast::function_call_expr_to_node(TSNode n) {
  TSNode func = child_by_field(n, "function");
  TSNode arg  = child_by_field(n, "argument");  // tuple

  // Method-call form `receiver.method(args)`: the `function` field is a
  // `dot_expression` (receiver '.' identifier). Split it so the call lowers
  // to `fcall(method, receiver, ...args)` — otherwise the whole dotted path
  // becomes the function-name ref and the receiver disappears.
  Lnast_node func_ref;
  Lnast_node receiver_ref;
  bool       has_receiver = false;
  if (ts_node_is_null(func)) {
    func_ref = Lnast_node::create_const("nil");
  } else if (std::string_view(ts_node_type(func)) == "dot_expression" && ts_node_named_child_count(func) >= 2) {
    uint32_t fnc         = ts_node_named_child_count(func);
    TSNode   method_node = ts_node_named_child(func, fnc - 1);
    func_ref             = Lnast_node::create_ref(trim(get_text(method_node)));
    if (fnc == 2) {
      // Single-level receiver: lower it as an expression and pass as arg0.
      receiver_ref = expr_to_node(ts_node_named_child(func, 0));
    } else {
      // Multi-level receiver `a.b.c()` → receiver is the tuple_get of the
      // leading dotted path; emit that as an inline tuple_get call.
      auto tg_idx = builder.add_child(Lnast_ntype::create_tuple_get());
      auto tg_ref = builder.mint_tmp_ref();
      lnast->add_child(tg_idx, tg_ref);
      lnast->add_child(tg_idx, expr_to_node(ts_node_named_child(func, 0)));
      for (uint32_t i = 1; i < fnc - 1; i++) {
        TSNode f = ts_node_named_child(func, i);
        lnast->add_child(tg_idx, Lnast_node::create_const(trim(get_text(f))));
      }
      receiver_ref = tg_ref;
    }
    has_receiver = true;
  } else {
    func_ref = Lnast_node::create_ref(trim(get_text(func)));
  }
  auto call_args = collect_call_args(arg);
  if (has_receiver) {
    Call_arg receiver_arg;
    receiver_arg.value = receiver_ref;
    call_args.insert(call_args.begin(), receiver_arg);
  }

  auto idx = builder.add_child(Lnast_ntype::create_func_call());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, func_ref);
  add_call_args_to_fcall(idx, call_args);
  return ref;
}

Lnast_node Prp2lnast::tuple_to_node(TSNode n, bool /*is_square*/) {
  uint32_t nnc = ts_node_named_child_count(n);

  // Pre-compute sub-expressions so their LNAST statements emit BEFORE the
  // tuple_add that references them — constprop runs in textual order.
  struct Item {
    bool        is_assign = false;
    bool        is_spread = false;  // `...expr` — emit value as its own concat chunk
    std::string assign_key;
    Lnast_node  value;
  };
  std::vector<Item> items;
  items.reserve(nnc);
  for (uint32_t i = 0; i < nnc; i++) {
    TSNode           c = ts_node_named_child(n, i);
    std::string_view t(ts_node_type(c));
    if (t == "unary_expression") {
      // Spread (`...inner`) is the only positional unary in a tuple item that
      // needs to flatten — every other unary just becomes a regular value.
      // The grammar aliases the spread token to `op_spread`, so dispatch by
      // the operator field's node kind.
      TSNode op_n = child_by_field(c, "operator");
      if (!ts_node_is_null(op_n) && std::string_view(ts_node_type(op_n)) == "op_spread") {
        TSNode arg_n = child_by_field(c, "argument");
        if (!ts_node_is_null(arg_n)) {
          Item it;
          it.is_spread = true;
          it.value     = expr_to_node(arg_n);
          items.push_back(std::move(it));
          continue;
        }
      }
    }
    if (t == "assignment") {
      Item it;
      it.is_assign = true;
      TSNode lv    = child_by_field(c, "lvalue");
      TSNode rv    = child_by_field(c, "rvalue");
      // The lvalue may carry a type_cast (`(a:u4=1, …)` parses lv as a
      // `typed_identifier`). Strip it: the ref text must be a bare
      // identifier; otherwise lnastfmt rejects the LNAST.
      std::string_view lvt2(ts_node_type(lv));
      if (lvt2 == "typed_identifier") {
        TSNode id = child_by_field(lv, "identifier");
        if (!ts_node_is_null(id)) {
          it.assign_key = trim(get_text(id));
        }
      } else {
        // `_complex_identifier` with optional sibling `type` field on the
        // assignment node. Use the lvalue text directly; for plain
        // identifiers it's already clean.
        it.assign_key = trim(get_text(lv));
      }
      if (it.assign_key.empty()) {
        // Anonymous slot (e.g. `(mut :u13 = 5)`) — synthesize a tmp name so
        // we don't emit an empty `ref` (lnastfmt would reject it).
        it.assign_key = builder.create_lnast_tmp();
      }
      if (!ts_node_is_null(rv)) {
        it.value = expr_to_node(rv);
      } else {
        TSNode op    = child_by_field(c, "operator");
        auto   start = ts_node_is_null(op) ? ts_node_end_byte(lv) : ts_node_end_byte(op);
        it.value     = constant_text_to_node(trim(text_between(start, ts_node_end_byte(c))));
      }
      items.push_back(std::move(it));
    } else if (t == "var_or_let_or_reg") {
      // New `_tuple_item` choice: `decl value` (e.g. `(mut 3, const 5)`).
      // `_tuple_item` is hidden so its children show as siblings on the
      // tuple. Pair this kind keyword with the next named sibling, which is
      // either a `typed_identifier` (lvalue declaration form) or an
      // expression (positional value with mutability override). Either way
      // the field is positional — record the value only.
      if (i + 1 < nnc) {
        TSNode           next = ts_node_named_child(n, i + 1);
        std::string_view nextt(ts_node_type(next));
        Item             it;
        if (nextt == "typed_identifier") {
          // Bare declaration like `mut b` — emit the identifier as the
          // tuple slot's positional value (initial value is undefined).
          TSNode id = child_by_field(next, "identifier");
          if (!ts_node_is_null(id)) {
            it.value = identifier_to_node(id, true);
          } else {
            it.value = builder.mint_tmp_ref();
          }
        } else {
          it.value = expr_to_node(next);
        }
        items.push_back(std::move(it));
        ++i;  // consumed the value sibling
      }
    } else if (t == "typed_identifier") {
      // Bare typed_identifier inside a tuple (no preceding decl keyword).
      // Treat as a positional ref slot.
      Item   it;
      TSNode id = child_by_field(c, "identifier");
      it.value  = ts_node_is_null(id) ? builder.mint_tmp_ref() : identifier_to_node(id, true);
      items.push_back(std::move(it));
    } else {
      Item it;
      it.value = expr_to_node(c);
      items.push_back(std::move(it));
    }
  }

  // Spread (`...inner`) items expand inline at the tuple's outer level; we
  // emit a tuple_concat of (chunks of non-spread items as their own
  // tuple_add tmps) interleaved with each spread's bundle ref. The simple
  // no-spread case stays as a single tuple_add.
  bool has_spread = false;
  for (const auto& it : items) {
    if (it.is_spread) {
      has_spread = true;
      break;
    }
  }

  auto emit_chunk_tuple_add = [&](const std::vector<Item>& chunk) -> Lnast_node {
    auto chunk_idx = builder.add_child(Lnast_ntype::create_tuple_add());
    auto chunk_ref = builder.mint_tmp_ref();
    lnast->add_child(chunk_idx, chunk_ref);
    for (const auto& it : chunk) {
      if (it.is_assign) {
        auto aidx = lnast->add_child(chunk_idx, Lnast_ntype::create_assign());
        lnast->add_child(aidx, Lnast_node::create_ref(it.assign_key));
        lnast->add_child(aidx, it.value);
      } else {
        lnast->add_child(chunk_idx, it.value);
      }
    }
    return chunk_ref;
  };

  if (!has_spread) {
    auto idx = builder.add_child(Lnast_ntype::create_tuple_add());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    if (nnc == 0) {
      // Tree-sitter-pyrope hides `_simple_number` (and other `_constant`) tokens,
      // so literals like `(10)` or `(-100)` parse as a tuple with zero named
      // children. Recover the literal from the text between `(` and `)`.
      auto inner = trim(text_between(ts_node_start_byte(n) + 1, ts_node_end_byte(n) - 1));
      if (!inner.empty()) {
        lnast->add_child(idx, constant_text_to_node(inner));
      }
      return ref;
    }
    for (auto& it : items) {
      if (it.is_assign) {
        auto aidx = lnast->add_child(idx, Lnast_ntype::create_assign());
        lnast->add_child(aidx, Lnast_node::create_ref(it.assign_key));
        lnast->add_child(aidx, it.value);
      } else {
        lnast->add_child(idx, it.value);
      }
    }
    return ref;
  }

  // Spread path: build a list of bundle refs to concat. Each contiguous run
  // of non-spread items becomes a tuple_add tmp; each spread contributes its
  // pre-evaluated inner ref directly. Concat is then a single tuple_concat
  // over those refs.
  std::vector<Lnast_node> chunks;
  std::vector<Item>       pending;
  for (auto& it : items) {
    if (it.is_spread) {
      if (!pending.empty()) {
        chunks.push_back(emit_chunk_tuple_add(pending));
        pending.clear();
      }
      chunks.push_back(it.value);
    } else {
      pending.push_back(std::move(it));
    }
  }
  if (!pending.empty()) {
    chunks.push_back(emit_chunk_tuple_add(pending));
  }

  auto idx = builder.add_child(Lnast_ntype::create_tuple_concat());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  for (const auto& c : chunks) {
    lnast->add_child(idx, c);
  }
  return ref;
}

// ---------------- Factory hook ----------------
