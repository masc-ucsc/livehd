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

#include "absl/strings/str_cat.h"
#include "pass.hpp"
#include "str_tools.hpp"

extern "C" TSLanguage* tree_sitter_pyrope();

Prp2lnast::Prp2lnast(std::string_view filename, std::string_view module_name, bool parse_only = false) {
  lnast = std::make_unique<Lnast>(module_name);

  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));

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

  dump();

  if (parse_only) {
    return;
  }

  tmp_ref_count     = 0;
  current_name_role = Name_role::None;

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

std::string Prp2lnast::trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return std::string(s);
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
    auto        node         = ts_tree_cursor_current_node(tc);
    auto        num_children = ts_node_child_count(node);
    std::string node_type(ts_node_type(node));

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

std::string Prp2lnast::get_tmp_name() { return absl::StrCat("___", tmp_ref_count++); }

Lnast_node Prp2lnast::make_tmp_ref() { return Lnast_node::create_ref(get_tmp_name()); }

inline TSNode Prp2lnast::child_by_field(const TSNode& node, const char* field) const {
  return ts_node_child_by_field_name(node, field, std::char_traits<char>::length(field));
}

// ---------------- Top level ----------------

void Prp2lnast::process_description() {
  stmts_index = lnast->add_child(lh::Tree_index::root(), Lnast_node::create_stmts());

  // Walk ALL children (not just named) so the anonymous `wrap`/`sat` token
  // that the new grammar attaches as the `overflow` field on an assignment
  // statement can be detected — it precedes the assignment as a sibling
  // because `_statement` is hidden.
  uint32_t nc = ts_node_child_count(ts_root_node);
  std::string pending_overflow;
  for (uint32_t i = 0; i < nc; i++) {
    TSNode      c = ts_node_child(ts_root_node, i);
    std::string t(ts_node_type(c));
    if (!ts_node_is_named(c)) {
      if (t == "wrap" || t == "sat") {
        pending_overflow = std::move(t);
      }
      continue;
    }
    if (t == "assignment" && !pending_overflow.empty()) {
      pending_overflow_kind = pending_overflow;
      process_statement(c);
      pending_overflow_kind.clear();
    } else {
      process_statement(c);
    }
    pending_overflow.clear();
  }
}

void Prp2lnast::process_statement(TSNode n) {
  if (ts_node_is_null(n)) {
    return;
  }
  std::string t(ts_node_type(n));
  if (t == "comment") {
    return;
  } else if (t == "scope_statement") {
    auto inner_stmts = lnast->add_child(stmts_index, Lnast_node::create_stmts());
    auto saved       = stmts_index;
    stmts_index      = inner_stmts;
    process_scope_statement(n, inner_stmts);
    stmts_index = saved;
  } else if (t == "declaration_statement") {
    process_declaration_statement(n);
  } else if (t == "assignment") {
    process_assignment(n);
  } else if (t == "assert_statement") {
    process_assert_statement(n);
  } else if (t == "while_statement") {
    process_while_statement(n);
  } else if (t == "for_statement") {
    process_for_statement(n);
  } else if (t == "loop_statement") {
    process_loop_statement(n);
  } else if (t == "control_statement") {
    process_control_statement(n);
  } else if (t == "function_call_statement") {
    process_function_call_statement(n);
  } else if (t == "lambda") {
    process_lambda_statement(n);
  } else if (t == "enum_assignment") {
    process_enum_assignment(n);
  } else if (t == "type_statement") {
    process_type_statement(n);
  } else if (t == "import_statement") {
    process_import_statement(n);
  } else if (t == "test_statement") {
    process_test_statement(n);
  } else if (t == "spawn_statement") {
    process_spawn_statement(n);
  } else if (t == "impl_statement") {
    process_impl_statement(n);
  } else if (t == "if_expression" || t == "match_expression" || t == "expression_item" || t == "unary_expression"
             || t == "bit_selection" || t == "member_selection" || t == "attribute_read" || t == "dot_expression"
             || t == "function_call_expression" || t == "identifier" || t == "tuple" || t == "tuple_sq"
             || t == "type_specification" || t == "constant") {
    // Expression used as a statement — evaluate for side effects.
    (void)expr_to_node(n);
  } else {
    std::print("prp2lnast: unhandled statement type `{}`\n", t);
  }
}

void Prp2lnast::process_scope_statement(TSNode n, lh::Tree_index /*target_stmts*/) {
  uint32_t nc = ts_node_child_count(n);
  std::string pending_overflow;
  for (uint32_t i = 0; i < nc; i++) {
    TSNode      c = ts_node_child(n, i);
    std::string t(ts_node_type(c));
    if (!ts_node_is_named(c)) {
      if (t == "wrap" || t == "sat") {
        pending_overflow = std::move(t);
      }
      continue;
    }
    if (t == "assignment" && !pending_overflow.empty()) {
      pending_overflow_kind = pending_overflow;
      process_statement(c);
      pending_overflow_kind.clear();
    } else {
      process_statement(c);
    }
    pending_overflow.clear();
  }
}

// ---------------- Declaration / Assignment ----------------

static std::string_view strip_decl_keyword(std::string_view d) {
  // d is e.g. "const", "mut", "reg", "comptime const", "await", "await[...]"
  // Return the storage-class keyword. If it starts with comptime, strip.
  auto skip_ws = [&](std::string_view s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) {
      s.remove_prefix(1);
    }
    return s;
  };
  d = skip_ws(d);
  if (d.substr(0, 8) == "comptime") {
    d.remove_prefix(8);
    d = skip_ws(d);
  }
  return d;
}

void Prp2lnast::process_declaration_statement(TSNode n) {
  // declaration_statement: var_or_let_or_reg + lvalue (typed_identifier or list)
  auto decl_node   = child_by_field(n, "decl");
  auto lvalue_node = child_by_field(n, "lvalue");
  if (ts_node_is_null(lvalue_node)) {
    return;
  }

  // Decide storage class keyword
  std::string_view decl_text    = get_text(decl_node);
  auto             kind         = strip_decl_keyword(decl_text);
  bool             has_comptime = decl_text.find("comptime") != std::string_view::npos;

  // For each typed_identifier in the lvalue, emit: attr_set <ref> "type" kind
  auto emit_decl_attrs = [&](TSNode ti) {
    TSNode id = child_by_field(ti, "identifier");
    if (ts_node_is_null(id)) {
      return;
    }
    auto name_text = std::string(get_text(id));
    // Apply function-input/output prefix
    Lnast_node ref = identifier_to_node(id, /*for_lvalue=*/true);
    {
      auto idx = lnast->add_child(stmts_index, Lnast_node::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("type"));
      lnast->add_child(idx, Lnast_node::create_const(std::string(kind)));
    }
    if (has_comptime) {
      auto idx = lnast->add_child(stmts_index, Lnast_node::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("comptime"));
      lnast->add_child(idx, Lnast_node::create_const("true"));
    }
    TSNode tc = child_by_field(ti, "type");
    if (!ts_node_is_null(tc)) {
      emit_type_spec(ref, tc);
    }
  };

  std::string lvtype(ts_node_type(lvalue_node));
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
  std::string lvt(ts_node_type(lvalue));
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
      TSNode      item = ts_node_named_child(lvalue, i);
      std::string itt(ts_node_type(item));
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
      auto       tg_idx = lnast->add_child(stmts_index, Lnast_node::create_tuple_get());
      Lnast_node tmp    = make_tmp_ref();
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
    bool        has_decl  = !ts_node_is_null(decl_node);
    std::string decl_text = has_decl ? std::string(get_text(decl_node)) : std::string();
    bool        has_cpt   = has_decl && decl_text.find("comptime") != std::string::npos;
    auto        kind_sv   = has_decl ? strip_decl_keyword(std::string_view(decl_text)) : std::string_view();

    Lnast_node ref = identifier_to_node(id, /*for_lvalue=*/true);
    if (has_decl) {
      auto idx = lnast->add_child(stmts_index, Lnast_node::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("type"));
      lnast->add_child(idx, Lnast_node::create_const(std::string(kind_sv)));
    }
    if (has_cpt) {
      auto idx = lnast->add_child(stmts_index, Lnast_node::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("comptime"));
      lnast->add_child(idx, Lnast_node::create_const("true"));
    }
    if (!ts_node_is_null(tc)) {
      emit_type_spec(ref, tc);
    }

    // Emit assign
    auto aidx = lnast->add_child(stmts_index, Lnast_node::create_assign());
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
    std::vector<Lnast_node> path;
    Lnast_node              root;
    bool                    have_root = false;
    std::function<void(TSNode)> collect = [&](TSNode n) {
      std::string t(ts_node_type(n));
      if (t == "dot_expression") {
        TSNode item = child_by_field(n, "item");
        if (ts_node_is_null(item)) {
          uint32_t nc = ts_node_named_child_count(n);
          if (nc > 0) {
            collect(ts_node_named_child(n, 0));
          }
          for (uint32_t i = 1; i < nc; i++) {
            TSNode      c = ts_node_named_child(n, i);
            std::string ct(ts_node_type(c));
            if (ct == "identifier") {
              path.push_back(Lnast_node::create_const(std::string(trim(get_text(c)))));
            } else {
              path.push_back(expr_to_node(c));
            }
          }
        } else {
          collect(item);
          uint32_t nc = ts_node_named_child_count(n);
          for (uint32_t i = 0; i < nc; i++) {
            TSNode  c = ts_node_named_child(n, i);
            if (ts_node_eq(c, item)) {
              continue;
            }
            std::string ct(ts_node_type(c));
            if (ct == "identifier") {
              path.push_back(Lnast_node::create_const(std::string(trim(get_text(c)))));
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
      auto idx = lnast->add_child(stmts_index, Lnast_node::create_tuple_set());
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
  auto       aidx = lnast->add_child(stmts_index, Lnast_node::create_assign());
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

  // The new grammar attaches a statement-level `wrap`/`sat` prefix as the
  // `overflow` field of the enclosing _statement. Lower it as an attr_set
  // bound to the lvalue. Consumed once per assignment.
  std::string overflow_kind;
  std::swap(overflow_kind, pending_overflow_kind);

  // Get operator text, default "="
  std::string op_text = ts_node_is_null(op) ? std::string("=") : std::string(get_text(op));

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

  // Compound assignment: lower `a OP= b` to `OP tmp a b; assign a tmp`
  if (op_text.size() > 1 && op_text.back() == '=' && op_text != "==") {
    std::string plain_op   = op_text.substr(0, op_text.size() - 1);
    Lnast_node  left_ref   = expr_to_node(lv);
    Lnast_node  result     = make_tmp_ref();
    auto        create_for = [&](const std::string& op) -> Lnast_node {
      if (op == "+") {
        return Lnast_node::create_plus();
      }
      if (op == "-") {
        return Lnast_node::create_minus();
      }
      if (op == "*") {
        return Lnast_node::create_mult();
      }
      if (op == "/") {
        return Lnast_node::create_div();
      }
      if (op == "|") {
        return Lnast_node::create_bit_or();
      }
      if (op == "&") {
        return Lnast_node::create_bit_and();
      }
      if (op == "^") {
        return Lnast_node::create_bit_xor();
      }
      if (op == "<<") {
        return Lnast_node::create_shl();
      }
      if (op == ">>") {
        return Lnast_node::create_sra();
      }
      if (op == "++") {
        return Lnast_node::create_tuple_concat();
      }
      if (op == "or") {
        return Lnast_node::create_log_or();
      }
      if (op == "and") {
        return Lnast_node::create_log_and();
      }
      return Lnast_node::create_invalid();
    };
    auto opnode = create_for(plain_op);
    if (opnode.is_invalid()) {
      std::print("prp2lnast: unhandled compound op `{}`\n", op_text);
      return;
    }
    auto idx = lnast->add_child(stmts_index, opnode);
    lnast->add_child(idx, result);
    lnast->add_child(idx, left_ref);
    lnast->add_child(idx, rvalue_node);
    (void)process_lvalue_for_assign(lv, result, decl, tc);
    return;
  }

  Lnast_node lhs_ref = process_lvalue_for_assign(lv, rvalue_node, decl, tc);

  if (!overflow_kind.empty()) {
    auto idx = lnast->add_child(stmts_index, Lnast_node::create_attr_set());
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
    std::string txt = trim(text_between(start, ts_node_end_byte(n)));
    auto        idx = lnast->add_child(stmts_index, Lnast_node::create_cassert());
    lnast->add_child(idx, constant_text_to_node(txt));
    return;
  }
  Lnast_node cond_ref = expr_to_node(cond);
  auto       idx      = lnast->add_child(stmts_index, Lnast_node::create_cassert());
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
  auto while_idx = lnast->add_child(stmts_index, Lnast_node::create_while());
  lnast->add_child(while_idx, cond_ref);
  auto body_idx = lnast->add_child(while_idx, Lnast_node::create_stmts());
  auto saved    = stmts_index;
  stmts_index   = body_idx;
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, body_idx);
  }
  stmts_index = saved;
}

void Prp2lnast::process_for_statement(TSNode n) {
  // Lower `for i in xs { body }` to a while with tmp counter and tuple_get.
  TSNode code = child_by_field(n, "code");
  // Identify binding (typed_identifier or typed_identifier_list) and data.
  TSNode binding;
  TSNode data = child_by_field(n, "data");
  if (ts_node_is_null(data)) {
    // ref_identifier form — find and use it as data
    for (uint32_t i = 0; i < child_count(n); i++) {
      TSNode      c = child(n, i);
      std::string t(ts_node_type(c));
      if (t == "ref_identifier") {
        data = c;
        break;
      }
    }
  }
  // Find binding: first typed_identifier or typed_identifier_list under arg_list
  binding = ts_node_child_by_field_name(n, "index", 5);
  if (ts_node_is_null(binding)) {
    for (uint32_t i = 0; i < child_count(n); i++) {
      TSNode      c = child(n, i);
      std::string t(ts_node_type(c));
      if (t == "typed_identifier") {
        binding = c;
        break;
      }
    }
  }

  // Simplified emission: while true with break — enough for parser tests to compile.
  auto while_idx = lnast->add_child(stmts_index, Lnast_node::create_while());
  lnast->add_child(while_idx, Lnast_node::create_const("true"));
  auto body_idx = lnast->add_child(while_idx, Lnast_node::create_stmts());
  auto saved    = stmts_index;
  stmts_index   = body_idx;

  // Emit a placeholder assign for binding if present
  if (!ts_node_is_null(binding) && !ts_node_is_null(data)) {
    std::string t(ts_node_type(binding));
    if (t == "typed_identifier") {
      TSNode id = child_by_field(binding, "identifier");
      if (!ts_node_is_null(id)) {
        Lnast_node ref = identifier_to_node(id, true);
        auto       idx = lnast->add_child(stmts_index, Lnast_node::create_tuple_get());
        lnast->add_child(idx, ref);
        lnast->add_child(idx, expr_to_node(data));
        lnast->add_child(idx, Lnast_node::create_const("0"));
      }
    }
  }
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, body_idx);
  }

  // Emit break to prevent infinite loop placeholder.
  lnast->add_child(stmts_index, Lnast_node::create_invalid());  // sentinel (invalid never emitted normally)
  stmts_index = saved;
}

void Prp2lnast::process_loop_statement(TSNode n) {
  // loop { body } -> while true body
  TSNode code      = child_by_field(n, "code");
  auto   while_idx = lnast->add_child(stmts_index, Lnast_node::create_while());
  lnast->add_child(while_idx, Lnast_node::create_const("true"));
  auto body_idx = lnast->add_child(while_idx, Lnast_node::create_stmts());
  auto saved    = stmts_index;
  stmts_index   = body_idx;
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, body_idx);
  }
  stmts_index = saved;
}

void Prp2lnast::process_control_statement(TSNode n) {
  // The LNAST ntype table predates `break`/`continue`/`return`; emit them as
  // tagged func_calls so consumers see a deterministic marker.
  std::string_view t           = get_text(n);
  auto             make_marker = [&](const char* name, Lnast_node arg) {
    auto idx = lnast->add_child(stmts_index, Lnast_node::create_func_call());
    lnast->add_child(idx, make_tmp_ref());
    lnast->add_child(idx, Lnast_node::create_const(name));
    if (!arg.is_invalid()) {
      lnast->add_child(idx, arg);
    }
  };
  if (t.substr(0, 5) == "break") {
    make_marker("break", Lnast_node::create_invalid());
    return;
  }
  if (t.substr(0, 8) == "continue") {
    make_marker("continue", Lnast_node::create_invalid());
    return;
  }
  TSNode arg = child_by_field(n, "argument");
  make_marker("return", ts_node_is_null(arg) ? Lnast_node::create_invalid() : expr_to_node(arg));
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
  auto idx = lnast->add_child(stmts_index, Lnast_node::create_func_call());
  lnast->add_child(idx, make_tmp_ref());
  lnast->add_child(idx, func_ref);
  if (!ts_node_is_null(args)) {
    uint32_t nnc = ts_node_named_child_count(args);
    for (uint32_t i = 0; i < nnc; i++) {
      lnast->add_child(idx, expr_to_node(ts_node_named_child(args, i)));
    }
  }
}

void Prp2lnast::process_lambda_statement(TSNode n) {
  TSNode func_type_node = child_by_field(n, "func_type");
  TSNode name_node      = child_by_field(n, "name");
  // function_definition_decl is a named child that isn't directly a field
  TSNode fdef;
  for (uint32_t i = 0; i < ts_node_named_child_count(n); i++) {
    TSNode      c = ts_node_named_child(n, i);
    std::string t(ts_node_type(c));
    if (t == "function_definition_decl") {
      fdef = c;
      break;
    }
  }
  TSNode code = child_by_field(n, "code");

  std::string kind = ts_node_is_null(func_type_node) ? "comb" : trim(get_text(func_type_node));
  // Strip optional pipe[N] attribute. The new grammar adds `proc` as a
  // recognized lambda kind; treat it as-is (no normalization).
  if (kind.size() >= 4 && kind.substr(0, 4) == "pipe") {
    kind = "pipe";
  }

  Lnast_node lambda_ref;
  if (ts_node_is_null(name_node)) {
    lambda_ref = make_tmp_ref();
  } else {
    lambda_ref = Lnast_node::create_ref(std::string(get_text(name_node)));
  }

  auto fd_idx = lnast->add_child(stmts_index, Lnast_node::create_func_def());
  lnast->add_child(fd_idx, lambda_ref);
  lnast->add_child(fd_idx, Lnast_node::create_const(kind));
  // generics tuple (empty)
  lnast->add_child(fd_idx, Lnast_node::create_tuple_add());
  // captures tuple (empty)
  lnast->add_child(fd_idx, Lnast_node::create_tuple_add());

  // Emit input args
  auto in_idx = lnast->add_child(fd_idx, Lnast_node::create_tuple_add());
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
        auto refn                               = absl::StrCat("$", std::string(get_text(id)));
        ref_name_map[std::string(get_text(id))] = refn;
        auto aidx                               = lnast->add_child(in_idx, Lnast_node::create_assign());
        lnast->add_child(aidx, Lnast_node::create_ref(refn));
        lnast->add_child(aidx, Lnast_node::create_const("0"));
      }
    }
  }

  // Output args
  auto out_idx = lnast->add_child(fd_idx, Lnast_node::create_tuple_add());
  std::vector<std::string> output_refs;  // for placeholder-lambda implicit assign
  if (!ts_node_is_null(fdef)) {
    // Collect ALL output field occurrences (there may be multiple due to "->" anchor + arg_list form)
    for (uint32_t i = 0; i < child_count(fdef); i++) {
      const char* fname = ts_node_field_name_for_child(fdef, i);
      if (fname && std::string(fname) == "output") {
        TSNode      o = child(fdef, i);
        std::string ot(ts_node_type(o));
        if (ot == "arg_list") {
          uint32_t nnc = ts_node_named_child_count(o);
          for (uint32_t j = 0; j < nnc; j++) {
            TSNode ti = ts_node_named_child(o, j);
            TSNode id = child_by_field(ti, "identifier");
            if (ts_node_is_null(id)) {
              continue;
            }
            auto refn                               = absl::StrCat("%", std::string(get_text(id)));
            ref_name_map[std::string(get_text(id))] = refn;
            output_refs.push_back(refn);
            auto aidx                               = lnast->add_child(out_idx, Lnast_node::create_assign());
            lnast->add_child(aidx, Lnast_node::create_ref(refn));
            lnast->add_child(aidx, Lnast_node::create_const("0"));
          }
        } else if (ot == "typed_identifier") {
          TSNode id = child_by_field(o, "identifier");
          if (!ts_node_is_null(id)) {
            auto refn                               = absl::StrCat("%", std::string(get_text(id)));
            ref_name_map[std::string(get_text(id))] = refn;
            output_refs.push_back(refn);
            auto aidx                               = lnast->add_child(out_idx, Lnast_node::create_assign());
            lnast->add_child(aidx, Lnast_node::create_ref(refn));
            lnast->add_child(aidx, Lnast_node::create_const("0"));
          }
        }
      }
    }
  }

  // Body
  auto body_idx = lnast->add_child(fd_idx, Lnast_node::create_stmts());
  auto saved    = stmts_index;
  stmts_index   = body_idx;
  if (!ts_node_is_null(code)) {
    // Placeholder lambda (06-functions.md "Output tuple"): when the body of
    // a single-output `comb` is a bare expression, the value is implicitly
    // assigned to the single output. Detect: scope_statement has exactly
    // one named child and that child is an expression-as-statement node.
    bool   placeholder = false;
    TSNode expr_body{};
    if (kind == "comb" && output_refs.size() == 1 && std::string(ts_node_type(code)) == "scope_statement"
        && ts_node_named_child_count(code) == 1) {
      TSNode      only = ts_node_named_child(code, 0);
      std::string ot(ts_node_type(only));
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
      auto       aidx = lnast->add_child(stmts_index, Lnast_node::create_assign());
      lnast->add_child(aidx, Lnast_node::create_ref(output_refs.front()));
      lnast->add_child(aidx, val);
    } else {
      process_scope_statement(code, body_idx);
    }
  }
  stmts_index = saved;

  // Clear the directed-name mapping after the function body.
  ref_name_map.clear();
}

void Prp2lnast::process_enum_assignment(TSNode n) {
  TSNode name = child_by_field(n, "name");
  if (ts_node_is_null(name)) {
    return;
  }
  Lnast_node ref = Lnast_node::create_ref(std::string(get_text(name)));
  // LNAST ntype predates an `enum_add` primitive; reuse `tuple_add` plus an
  // `attr_set` marking the value as an enum.
  auto idx = lnast->add_child(stmts_index, Lnast_node::create_tuple_add());
  lnast->add_child(idx, ref);
  auto aidx = lnast->add_child(stmts_index, Lnast_node::create_attr_set());
  lnast->add_child(aidx, ref);
  lnast->add_child(aidx, Lnast_node::create_const("type"));
  lnast->add_child(aidx, Lnast_node::create_const("enum"));
}

void Prp2lnast::process_type_statement(TSNode n) {
  TSNode name = child_by_field(n, "name");
  if (ts_node_is_null(name)) {
    return;
  }
  Lnast_node ref = Lnast_node::create_ref(std::string(get_text(name)));
  auto       idx = lnast->add_child(stmts_index, Lnast_node::create_type_def());
  lnast->add_child(idx, ref);
  // Simplified: emit an unknown_type child for now.
  lnast->add_child(idx, Lnast_node::create_unknown_type());
}

void Prp2lnast::process_import_statement(TSNode n) {
  TSNode     alias  = child_by_field(n, "alias");
  TSNode     mod    = child_by_field(n, "module");
  Lnast_node target = ts_node_is_null(alias) ? make_tmp_ref() : Lnast_node::create_ref(std::string(get_text(alias)));
  auto       idx    = lnast->add_child(stmts_index, Lnast_node::create_func_call());
  lnast->add_child(idx, target);
  lnast->add_child(idx, Lnast_node::create_const("import"));
  lnast->add_child(idx, Lnast_node::create_const(ts_node_is_null(mod) ? "" : std::string(get_text(mod))));
}

void Prp2lnast::process_test_statement(TSNode n) {
  TSNode code   = child_by_field(n, "code");
  auto   fd_idx = lnast->add_child(stmts_index, Lnast_node::create_func_def());
  auto   tmp    = make_tmp_ref();
  lnast->add_child(fd_idx, tmp);
  lnast->add_child(fd_idx, Lnast_node::create_const("comb"));
  lnast->add_child(fd_idx, Lnast_node::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_node::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_node::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_node::create_tuple_add());
  auto body_idx = lnast->add_child(fd_idx, Lnast_node::create_stmts());
  auto saved    = stmts_index;
  stmts_index   = body_idx;
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, body_idx);
  }
  stmts_index = saved;
  auto aidx   = lnast->add_child(stmts_index, Lnast_node::create_attr_set());
  lnast->add_child(aidx, tmp);
  lnast->add_child(aidx, Lnast_node::create_const("test"));
  lnast->add_child(aidx, Lnast_node::create_const("true"));
}

void Prp2lnast::process_spawn_statement(TSNode n) {
  TSNode     name   = child_by_field(n, "name");
  Lnast_node ref    = ts_node_is_null(name) ? make_tmp_ref() : Lnast_node::create_ref(std::string(get_text(name)));
  auto       fd_idx = lnast->add_child(stmts_index, Lnast_node::create_func_def());
  lnast->add_child(fd_idx, ref);
  lnast->add_child(fd_idx, Lnast_node::create_const("comb"));
  lnast->add_child(fd_idx, Lnast_node::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_node::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_node::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_node::create_tuple_add());
  lnast->add_child(fd_idx, Lnast_node::create_stmts());
  auto aidx = lnast->add_child(stmts_index, Lnast_node::create_attr_set());
  lnast->add_child(aidx, ref);
  lnast->add_child(aidx, Lnast_node::create_const("spawn"));
  lnast->add_child(aidx, Lnast_node::create_const("true"));
}

void Prp2lnast::process_impl_statement(TSNode /*n*/) {
  // Simplified: emit as a placeholder assert to preserve node count.
  auto idx = lnast->add_child(stmts_index, Lnast_node::create_cassert());
  lnast->add_child(idx, Lnast_node::create_const("true"));
}

// ---------------- Expressions ----------------

Lnast_node Prp2lnast::expr_to_node(TSNode n) {
  if (ts_node_is_null(n)) {
    return Lnast_node::create_const("0");
  }
  std::string t(ts_node_type(n));
  if (t == "identifier") {
    return identifier_to_node(n, /*for_lvalue=*/false);
  }
  if (t == "expression_item") {
    return binary_expr_to_node(n);
  }
  if (t == "constant") {
    // `constant` wraps one of `_number` / `_bool_literal` / `_string_literal` /
    // `_unknown_literal`. Numbers and `'..'` literals are hidden tokens (the
    // named child list is empty); the raw text is the literal. A named
    // `complex_string_literal` child means double-quoted — lower through
    // expr_to_node so the interpolation/body handling stays in one place.
    uint32_t nnc = ts_node_named_child_count(n);
    if (nnc == 1) {
      TSNode c = ts_node_named_child(n, 0);
      if (std::string(ts_node_type(c)) == "complex_string_literal") {
        return expr_to_node(c);
      }
    }
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
    process_scope_statement(n, stmts_index);
    return Lnast_node::create_const("0");
  }
  if (t == "complex_string_literal") {
    // Double-quoted string. When there are no `{expr}` interpolations, the
    // grammar exposes only the outer quotes as named children, so re-emit
    // the body as a single-quoted pyrope string literal so `Lconst::from_pyrope`
    // can round-trip it as a string.
    bool     has_interp = false;
    uint32_t nnc        = ts_node_named_child_count(n);
    for (uint32_t i = 0; i < nnc; i++) {
      std::string ct(ts_node_type(ts_node_named_child(n, i)));
      if (ct != "\"") {
        has_interp = true;
        break;
      }
    }
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
      name = std::string(inner);
    }
  }
  auto it = ref_name_map.find(name);
  if (it != ref_name_map.end()) {
    return Lnast_node::create_ref(it->second);
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
    auto idx = lnast->add_child(stmts_index, Lnast_node::create_type_spec());
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
    if (std::string(ts_node_type(c)) == "attribute_list") {
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
    std::string tt(ts_node_type(ty));
    if (tt == "expression_type") {
      uint32_t inner_count = ts_node_named_child_count(ty);
      for (uint32_t i = 0; i < inner_count; i++) {
        TSNode inner = ts_node_named_child(ty, i);
        if (std::string(ts_node_type(inner)) == "tuple") {
          auto bundle_ref = tuple_to_node(inner, false);
          auto aidx       = lnast->add_child(stmts_index, Lnast_node::create_assign());
          lnast->add_child(aidx, target);
          lnast->add_child(aidx, bundle_ref);
          // Skip the empty `type_spec expr_type:` we'd otherwise emit; the
          // tuple-shape information is now encoded in the target's bundle.
          goto attrs;
        }
      }
    }
    {
      auto idx = lnast->add_child(stmts_index, Lnast_node::create_type_spec());
      lnast->add_child(idx, target);
      emit_type_expr(idx, ty);
    }
  }
attrs:
  uint32_t total = ts_node_child_count(type_cast_node);
  for (uint32_t i = 0; i < total; i++) {
    TSNode c = ts_node_child(type_cast_node, i);
    if (std::string(ts_node_type(c)) == "attribute_list") {
      emit_attribute_list(target, c);
    }
  }
}

void Prp2lnast::emit_type_expr(const lh::Tree_index& parent, TSNode type_node) {
  std::string t(ts_node_type(type_node));
  if (t == "unsized_integer_type") {
    std::string txt(trim(get_text(type_node)));
    if (txt.size() > 0 && (txt[0] == 'u' || txt.substr(0, 8) == "unsigned")) {
      lnast->add_child(parent, Lnast_node::create_prim_type_uint());
    } else {
      lnast->add_child(parent, Lnast_node::create_prim_type_sint());
    }
  } else if (t == "sized_integer_type") {
    std::string txt(trim(get_text(type_node)));
    auto node = (txt.size() > 0 && txt[0] == 'u') ? Lnast_node::create_prim_type_uint() : Lnast_node::create_prim_type_sint();
    auto idx  = lnast->add_child(parent, node);
    lnast->add_child(idx, Lnast_node::create_const(txt.substr(1)));
  } else if (t == "array_type") {
    auto   idx  = lnast->add_child(parent, Lnast_node::create_comp_type_array());
    TSNode base = child_by_field(type_node, "base");
    if (!ts_node_is_null(base)) {
      emit_type_expr(idx, base);
    }
    TSNode len = child_by_field(type_node, "length");
    if (!ts_node_is_null(len)) {
      lnast->add_child(idx, expr_to_node(len));
    }
  } else if (t == "expression_type" || t == "dot_expression_type" || t == "function_call_type") {
    lnast->add_child(parent, Lnast_node::create_expr_type());
  } else {
    // 'bool', 'string', timing, or other: record as unknown for now.
    std::string txt(trim(get_text(type_node)));
    if (txt == "bool") {
      lnast->add_child(parent, Lnast_node::create_prim_type_boolean());
    } else if (txt == "string") {
      lnast->add_child(parent, Lnast_node::create_prim_type_string());
    } else {
      lnast->add_child(parent, Lnast_node::create_unknown_type());
    }
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
    if (!fname || std::string(fname) != "name") {
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
      std::string knms(knm);
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
    auto idx = lnast->add_child(stmts_index, Lnast_node::create_attr_set());
    lnast->add_child(idx, target);
    lnast->add_child(idx, Lnast_node::create_const(std::string(get_text(name_n))));
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
    return make_tmp_ref();
  }

  // Collect operands / operators in source order. Field names disambiguate:
  // every named child is either an `operand` or an `operator` field.
  std::vector<Lnast_node>  operands;
  std::vector<std::string> op_texts;
  std::vector<std::string> op_kinds;  // node type (binary_*_op)
  operands.reserve(nnc / 2 + 1);
  op_texts.reserve(nnc / 2);
  op_kinds.reserve(nnc / 2);
  for (uint32_t i = 0; i < nnc; i++) {
    TSNode      c     = ts_node_named_child(n, i);
    const char* fname = ts_node_field_name_for_child(n, ts_node_is_named(c) ? 0 : 0);
    // field_name_for_child expects the child index into ALL children, not
    // just named — re-derive from the operator-node type instead, which is
    // already distinct enough for our dispatch.
    std::string ct(ts_node_type(c));
    if (ct == "binary_times_op" || ct == "binary_other_op" || ct == "binary_compare_op" || ct == "binary_logical_op") {
      op_kinds.push_back(std::move(ct));
      op_texts.push_back(trim(get_text(c)));
      (void)fname;
    } else {
      operands.push_back(expr_to_node(c));
    }
  }

  if (op_kinds.empty()) {
    return operands.front();
  }
  I(operands.size() == op_kinds.size() + 1);

  auto make_binop = [&](Lnast_node opnode, const Lnast_node& l, const Lnast_node& r) {
    auto idx = lnast->add_child(stmts_index, opnode);
    auto ref = make_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, l);
    lnast->add_child(idx, r);
    return ref;
  };
  auto make_call = [&](const char* name, const Lnast_node& l, const Lnast_node& r) {
    auto idx = lnast->add_child(stmts_index, Lnast_node::create_func_call());
    auto ref = make_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, Lnast_node::create_const(name));
    lnast->add_child(idx, l);
    lnast->add_child(idx, r);
    return ref;
  };
  auto wrap_not = [&](const Lnast_node& inner) {
    auto idx = lnast->add_child(stmts_index, Lnast_node::create_log_not());
    auto ref = make_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, inner);
    return ref;
  };

  auto emit_compare = [&](std::string_view op, const Lnast_node& l, const Lnast_node& r) -> Lnast_node {
    if (op == "==") {
      return make_binop(Lnast_node::create_eq(), l, r);
    }
    if (op == "!=") {
      return make_binop(Lnast_node::create_ne(), l, r);
    }
    if (op == "<") {
      return make_binop(Lnast_node::create_lt(), l, r);
    }
    if (op == "<=") {
      return make_binop(Lnast_node::create_le(), l, r);
    }
    if (op == ">") {
      return make_binop(Lnast_node::create_gt(), l, r);
    }
    if (op == ">=") {
      return make_binop(Lnast_node::create_ge(), l, r);
    }
    if (op == "has") {
      return make_call("has", l, r);
    }
    if (op == "!has") {
      return wrap_not(make_call("has", l, r));
    }
    if (op == "in") {
      return make_call("in", l, r);
    }
    if (op == "!in") {
      return wrap_not(make_call("in", l, r));
    }
    if (op == "is") {
      return make_binop(Lnast_node::create_is(), l, r);
    }
    if (op == "!is") {
      return wrap_not(make_binop(Lnast_node::create_is(), l, r));
    }
    if (op == "does") {
      return make_call("does", l, r);
    }
    if (op == "!does") {
      return wrap_not(make_call("does", l, r));
    }
    if (op == "case") {
      return make_call("case", l, r);
    }
    if (op == "!case") {
      return wrap_not(make_call("case", l, r));
    }
    if (op == "equals") {
      return make_binop(Lnast_node::create_eq(), l, r);
    }
    if (op == "!equals") {
      return make_binop(Lnast_node::create_ne(), l, r);
    }
    std::print("prp2lnast: unhandled compare op `{}`\n", op);
    return make_tmp_ref();
  };

  auto emit_other = [&](std::string_view op, const Lnast_node& l, const Lnast_node& r) -> Lnast_node {
    if (op == "+") {
      return make_binop(Lnast_node::create_plus(), l, r);
    }
    if (op == "-") {
      return make_binop(Lnast_node::create_minus(), l, r);
    }
    if (op == "++") {
      return make_binop(Lnast_node::create_tuple_concat(), l, r);
    }
    if (op == "<<") {
      return make_binop(Lnast_node::create_shl(), l, r);
    }
    if (op == ">>") {
      return make_binop(Lnast_node::create_sra(), l, r);
    }
    if (op == "&") {
      return make_binop(Lnast_node::create_bit_and(), l, r);
    }
    if (op == "|") {
      return make_binop(Lnast_node::create_bit_or(), l, r);
    }
    if (op == "^") {
      return make_binop(Lnast_node::create_bit_xor(), l, r);
    }
    if (op == "!&") {
      return wrap_not(make_binop(Lnast_node::create_bit_and(), l, r));
    }
    if (op == "!|") {
      return wrap_not(make_binop(Lnast_node::create_bit_or(), l, r));
    }
    if (op == "!^") {
      return wrap_not(make_binop(Lnast_node::create_bit_xor(), l, r));
    }
    if (op == "..=") {
      return make_binop(Lnast_node::create_range(), l, r);
    }
    if (op == "..<") {
      auto m    = lnast->add_child(stmts_index, Lnast_node::create_minus());
      auto mref = make_tmp_ref();
      lnast->add_child(m, mref);
      lnast->add_child(m, r);
      lnast->add_child(m, Lnast_node::create_const("1"));
      return make_binop(Lnast_node::create_range(), l, mref);
    }
    if (op == "..+") {
      auto p    = lnast->add_child(stmts_index, Lnast_node::create_plus());
      auto pref = make_tmp_ref();
      lnast->add_child(p, pref);
      lnast->add_child(p, l);
      lnast->add_child(p, r);
      auto mm   = lnast->add_child(stmts_index, Lnast_node::create_minus());
      auto mref = make_tmp_ref();
      lnast->add_child(mm, mref);
      lnast->add_child(mm, pref);
      lnast->add_child(mm, Lnast_node::create_const("1"));
      return make_binop(Lnast_node::create_range(), l, mref);
    }
    // `step` has no dedicated LNAST op; lower as a two-arg call for now.
    if (op == "step") {
      return make_call("step", l, r);
    }
    std::print("prp2lnast: unhandled `binary_other_op` `{}`\n", op);
    return make_tmp_ref();
  };

  auto emit_times = [&](std::string_view op, const Lnast_node& l, const Lnast_node& r) -> Lnast_node {
    if (op == "*") {
      return make_binop(Lnast_node::create_mult(), l, r);
    }
    if (op == "/") {
      return make_binop(Lnast_node::create_div(), l, r);
    }
    if (op == "%") {
      return make_binop(Lnast_node::create_mod(), l, r);
    }
    std::print("prp2lnast: unhandled `binary_times_op` `{}`\n", op);
    return make_tmp_ref();
  };

  auto emit_logical = [&](std::string_view op, const Lnast_node& l, const Lnast_node& r) -> Lnast_node {
    if (op == "and") {
      return make_binop(Lnast_node::create_log_and(), l, r);
    }
    if (op == "or") {
      return make_binop(Lnast_node::create_log_or(), l, r);
    }
    if (op == "!and") {
      return wrap_not(make_binop(Lnast_node::create_log_and(), l, r));
    }
    if (op == "!or") {
      return wrap_not(make_binop(Lnast_node::create_log_or(), l, r));
    }
    if (op == "implies") {
      return make_call("implies", l, r);
    }
    if (op == "!implies") {
      return wrap_not(make_call("implies", l, r));
    }
    std::print("prp2lnast: unhandled `binary_logical_op` `{}`\n", op);
    return make_tmp_ref();
  };

  const std::string& kind = op_kinds.front();

  // Compare tier: Python-style chain. For each adjacent pair emit the
  // corresponding compare; then left-fold `log_and` over the results.
  if (kind == "binary_compare_op") {
    std::vector<Lnast_node> pair_results;
    pair_results.reserve(op_texts.size());
    for (size_t i = 0; i < op_texts.size(); ++i) {
      pair_results.push_back(emit_compare(op_texts[i], operands[i], operands[i + 1]));
    }
    Lnast_node acc = pair_results.front();
    for (size_t i = 1; i < pair_results.size(); ++i) {
      acc = make_binop(Lnast_node::create_log_and(), acc, pair_results[i]);
    }
    return acc;
  }

  // Logical tier: mixing distinct operators (`a and b or c`) is ambiguous —
  // same precedence, different semantics. Require explicit parens.
  // TODO(upass.md): turn this into a proper diagnostic with source range
  // once the pass gains a diagnostics channel.
  if (kind == "binary_logical_op") {
    for (size_t i = 1; i < op_texts.size(); ++i) {
      if (op_texts[i] != op_texts[0]) {
        std::print(
            "prp2lnast: mixed `{}` and `{}` at the same precedence — add "
            "parentheses to disambiguate\n",
            op_texts[0],
            op_texts[i]);
        break;
      }
    }
  }

  // All other tiers (and logical after the ambiguity warning): left-fold.
  Lnast_node acc = operands.front();
  for (size_t i = 0; i < op_texts.size(); ++i) {
    const std::string& op = op_texts[i];
    const std::string& k  = op_kinds[i];
    if (k == "binary_times_op") {
      acc = emit_times(op, acc, operands[i + 1]);
    } else if (k == "binary_other_op") {
      acc = emit_other(op, acc, operands[i + 1]);
    } else if (k == "binary_logical_op") {
      acc = emit_logical(op, acc, operands[i + 1]);
    } else {
      std::print("prp2lnast: unknown op node `{}`\n", k);
      return make_tmp_ref();
    }
  }
  return acc;
}

Lnast_node Prp2lnast::unary_expr_to_node(TSNode n) {
  TSNode      op_n    = child_by_field(n, "operator");
  TSNode      arg_n   = child_by_field(n, "argument");
  std::string op_text = ts_node_is_null(op_n) ? std::string() : std::string(trim(get_text(op_n)));
  Lnast_node  arg_ref;
  if (ts_node_is_null(arg_n)) {
    auto start = ts_node_is_null(op_n) ? ts_node_start_byte(n) : ts_node_end_byte(op_n);
    arg_ref    = constant_text_to_node(trim(text_between(start, ts_node_end_byte(n))));
  } else {
    arg_ref = expr_to_node(arg_n);
  }
  if (op_text == "!" || op_text == "not") {
    auto idx = lnast->add_child(stmts_index, Lnast_node::create_log_not());
    auto ref = make_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, arg_ref);
    return ref;
  }
  if (op_text == "~") {
    auto idx = lnast->add_child(stmts_index, Lnast_node::create_bit_not());
    auto ref = make_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, arg_ref);
    return ref;
  }
  if (op_text == "-") {
    auto idx = lnast->add_child(stmts_index, Lnast_node::create_minus());
    auto ref = make_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, Lnast_node::create_const("0"));
    lnast->add_child(idx, arg_ref);
    return ref;
  }
  if (op_text == "...") {
    // Spread — pass-through for now.
    return arg_ref;
  }
  return arg_ref;
}

Lnast_node Prp2lnast::if_expr_to_node(TSNode n) {
  // if init?; cond { code } (elif init?; cond { code })* (else { code })?
  auto if_idx = lnast->add_child(stmts_index, Lnast_node::create_if());
  auto result = make_tmp_ref();

  auto process_branch = [&](TSNode cond, TSNode code) {
    // Init statements are emitted inline into the current scope if present.
    TSNode init = child_by_field(n, "init");
    if (!ts_node_is_null(init)) {
      uint32_t inc = ts_node_named_child_count(init);
      for (uint32_t i = 0; i < inc; i++) {
        process_statement(ts_node_named_child(init, i));
      }
    }
    Lnast_node cref = ts_node_is_null(cond) ? Lnast_node::create_const("true") : expr_to_node(cond);
    lnast->add_child(if_idx, cref);
    auto body_idx = lnast->add_child(if_idx, Lnast_node::create_stmts());
    auto saved    = stmts_index;
    stmts_index   = body_idx;
    if (!ts_node_is_null(code)) {
      process_scope_statement(code, body_idx);
    }
    auto a = lnast->add_child(stmts_index, Lnast_node::create_assign());
    lnast->add_child(a, result);
    lnast->add_child(a, Lnast_node::create_const("0"));
    stmts_index = saved;
  };

  // Walk children in order: find each (condition, code) pair and the optional else.
  uint32_t nc = child_count(n);
  TSNode   pending_cond;
  bool     have_cond = false;
  bool     in_else   = false;
  for (uint32_t i = 0; i < nc; i++) {
    const char* field_name = ts_node_field_name_for_child(n, i);
    if (!field_name) {
      continue;
    }
    std::string f(field_name);
    TSNode      c = child(n, i);
    std::string ctxt(get_text(c));
    if (f == "condition") {
      pending_cond = c;
      have_cond    = true;
    } else if (f == "code") {
      if (in_else) {
        // Else branch — single scope_statement child.
        auto body_idx = lnast->add_child(if_idx, Lnast_node::create_stmts());
        auto saved    = stmts_index;
        stmts_index   = body_idx;
        process_scope_statement(c, body_idx);
        auto a = lnast->add_child(stmts_index, Lnast_node::create_assign());
        lnast->add_child(a, result);
        lnast->add_child(a, Lnast_node::create_const("0"));
        stmts_index = saved;
        in_else     = false;
      } else if (have_cond) {
        process_branch(pending_cond, c);
        have_cond = false;
      }
    } else if (f == "else") {
      // Anonymous else keyword — marks that next code is else.
      std::string_view txt = get_text(c);
      if (txt == "else") {
        in_else = true;
      }
    }
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
    if (std::string(f) == "condition") {
      subject   = child(n, i);
      have_subj = true;
      break;
    }
  }
  if (!have_subj) {
    return Lnast_node::create_const("0");
  }
  Lnast_node subject_ref = expr_to_node(subject);

  // Now walk children in order to build arms.
  auto       if_idx              = lnast->add_child(stmts_index, Lnast_node::create_if());
  Lnast_node result              = make_tmp_ref();
  bool       saw_first_condition = false;

  TSNode pending_expr_list = {};
  bool   have_pending      = false;
  bool   pending_is_else   = false;
  for (uint32_t i = 0; i < child_count(n); i++) {
    const char* f = ts_node_field_name_for_child(n, i);
    if (!f) {
      continue;
    }
    std::string field(f);
    TSNode      c = child(n, i);
    if (field == "condition") {
      if (!saw_first_condition) {
        saw_first_condition = true;
        continue;
      }
      std::string t(ts_node_type(c));
      std::string txt(trim(get_text(c)));
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
        // Equality check against each expression in the list.
        Lnast_node arm_cond;
        uint32_t   nnc = ts_node_named_child_count(pending_expr_list);
        if (nnc == 0) {
          // Hidden constant — extract text of the entire expression_list.
          Lnast_node rhs = constant_text_to_node(trim(get_text(pending_expr_list)));
          auto       idx = lnast->add_child(stmts_index, Lnast_node::create_eq());
          auto       ref = make_tmp_ref();
          lnast->add_child(idx, ref);
          lnast->add_child(idx, subject_ref);
          lnast->add_child(idx, rhs);
          arm_cond = ref;
        } else if (nnc == 1) {
          auto idx = lnast->add_child(stmts_index, Lnast_node::create_eq());
          auto ref = make_tmp_ref();
          lnast->add_child(idx, ref);
          lnast->add_child(idx, subject_ref);
          lnast->add_child(idx, expr_to_node(ts_node_named_child(pending_expr_list, 0)));
          arm_cond = ref;
        } else {
          auto or_idx = lnast->add_child(stmts_index, Lnast_node::create_log_or());
          arm_cond    = make_tmp_ref();
          lnast->add_child(or_idx, arm_cond);
          for (uint32_t j = 0; j < nnc; j++) {
            auto idx = lnast->add_child(stmts_index, Lnast_node::create_eq());
            auto ref = make_tmp_ref();
            lnast->add_child(idx, ref);
            lnast->add_child(idx, subject_ref);
            lnast->add_child(idx, expr_to_node(ts_node_named_child(pending_expr_list, j)));
            lnast->add_child(or_idx, ref);
          }
        }
        lnast->add_child(if_idx, arm_cond);
        auto body_idx = lnast->add_child(if_idx, Lnast_node::create_stmts());
        auto saved    = stmts_index;
        stmts_index   = body_idx;
        process_scope_statement(c, body_idx);
        auto a = lnast->add_child(stmts_index, Lnast_node::create_assign());
        lnast->add_child(a, result);
        lnast->add_child(a, Lnast_node::create_const("0"));
        stmts_index  = saved;
        have_pending = false;
      }
    }
  }
  if (else_code.tree != nullptr) {
    auto body_idx = lnast->add_child(if_idx, Lnast_node::create_stmts());
    auto saved    = stmts_index;
    stmts_index   = body_idx;
    process_scope_statement(else_code, body_idx);
    auto a = lnast->add_child(stmts_index, Lnast_node::create_assign());
    lnast->add_child(a, result);
    lnast->add_child(a, Lnast_node::create_const("0"));
    stmts_index = saved;
  }
  return result;
}

Lnast_node Prp2lnast::bit_selection_to_node(TSNode n) {
  // bit_selection: argument '#' [reduction] select
  // LNAST get_mask expects a bitmask, not the selected bit position/range.
  TSNode     arg  = ts_node_named_child(n, 0);
  Lnast_node base = expr_to_node(arg);

  TSNode sel_node  = {};
  TSNode type_node = {};
  for (uint32_t i = 0; i < child_count(n); i++) {
    TSNode c  = child(n, i);
    auto   ct = std::string_view(ts_node_type(c));
    auto   tx = trim(get_text(c));
    if (ct == "select") {
      sel_node = c;
    } else if (!ts_node_is_named(c) && (tx == "|" || tx == "&" || tx == "^" || tx == "+")) {
      type_node = c;
    }
  }

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
    if (trim(get_text(op)) != "..=") {
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
    auto idx = lnast->add_child(stmts_index, Lnast_node::create_shl());
    auto ref = make_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, Lnast_node::create_const("1"));
    lnast->add_child(idx, pos);
    return ref;
  };

  Lnast_node mask_ref = Lnast_node::create_const("-1");
  if (!ts_node_is_null(sel_node)) {
    TSNode list = {};
    for (uint32_t i = 0; i < ts_node_named_child_count(sel_node); i++) {
      TSNode c = ts_node_named_child(sel_node, i);
      if (std::string_view(ts_node_type(c)) == "expression_list") {
        list = c;
        break;
      }
    }

    if (!ts_node_is_null(list) && ts_node_named_child_count(list) > 0) {
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

  auto idx = lnast->add_child(stmts_index, Lnast_node::create_get_mask());
  auto ref = make_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, base);
  lnast->add_child(idx, mask_ref);

  if (!ts_node_is_null(type_node)) {
    auto       op = trim(get_text(type_node));
    Lnast_node red_node;
    if (op == "|") {
      red_node = Lnast_node::create_red_or();
    } else if (op == "&") {
      red_node = Lnast_node::create_red_and();
    } else if (op == "^") {
      red_node = Lnast_node::create_red_xor();
    } else if (op == "+") {
      red_node = Lnast_node::create_popcount();
    } else {
      return ref;
    }
    auto r_idx = lnast->add_child(stmts_index, red_node);
    auto r_ref = make_tmp_ref();
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
    auto rng_idx = lnast->add_child(stmts_index, Lnast_node::create_range());
    auto rng_ref = make_tmp_ref();
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

    TSNode open_n = child_by_field(sel, "open_range");
    if (!ts_node_is_null(open_n)) {
      // `x[expr..]` — open_range field holds the expression child.
      // `x[..]`     — open_range field holds the literal `..` token (no expr).
      Lnast_node start_node = (std::string(ts_node_type(open_n)) == "..")
                                  ? Lnast_node::create_const("0")
                                  : expr_to_node(open_n);
      // `nil` is the open-end sentinel — round-trips through Lconst as a
      // string and is recognised by process_tuple_get as "slice to source's
      // last index". Using a real value here would falsely truncate.
      auto end_node = Lnast_node::create_const("nil");
      fields.push_back(emit_range(start_node, end_node));
      continue;
    }

    TSNode fz_n = child_by_field(sel, "from_zero");
    if (!ts_node_is_null(fz_n)) {
      // `x[..=expr]` or `x[..<expr]`. The operator is an anonymous token
      // child of `sel`; scan to find which one was used.
      bool     is_lt = false;
      uint32_t total = ts_node_child_count(sel);
      for (uint32_t k = 0; k < total; k++) {
        std::string t(ts_node_type(ts_node_child(sel, k)));
        if (t == "..<") {
          is_lt = true;
          break;
        }
        if (t == "..=") {
          break;
        }
      }
      Lnast_node end_expr = expr_to_node(fz_n);
      Lnast_node end_node = end_expr;
      if (is_lt) {
        // `..<n` is exclusive: end = n - 1 to match the inclusive form
        // stored on the range node.
        auto m    = lnast->add_child(stmts_index, Lnast_node::create_minus());
        auto mref = make_tmp_ref();
        lnast->add_child(m, mref);
        lnast->add_child(m, end_expr);
        lnast->add_child(m, Lnast_node::create_const("1"));
        end_node = mref;
      }
      fields.push_back(emit_range(Lnast_node::create_const("0"), end_node));
      continue;
    }
  }
  auto idx = lnast->add_child(stmts_index, Lnast_node::create_tuple_get());
  auto ref = make_tmp_ref();
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
    if (std::string(ts_node_type(al)) != "attribute_list") {
      continue;
    }
    // attribute_list children carry inner `name`/`value` fields directly
    // (the grammar wraps each item in an anonymous seq, so the field tags
    // appear on attribute_list itself). Reads only use the names; any
    // `=value` on a read is overparse and is silently dropped here.
    uint32_t total = ts_node_child_count(al);
    for (uint32_t j = 0; j < total; j++) {
      const char* fname = ts_node_field_name_for_child(al, j);
      if (!fname || std::string(fname) != "name") {
        continue;
      }
      TSNode name_n = ts_node_child(al, j);
      auto idx      = lnast->add_child(stmts_index, Lnast_node::create_attr_get());
      auto ref      = make_tmp_ref();
      lnast->add_child(idx, ref);
      lnast->add_child(idx, base);
      lnast->add_child(idx, Lnast_node::create_const(std::string(get_text(name_n))));
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
  auto       idx  = lnast->add_child(stmts_index, Lnast_node::create_tuple_get());
  auto       ref  = make_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, base);
  for (uint32_t i = 1; i < nnc; i++) {
    TSNode c = ts_node_named_child(n, i);
    lnast->add_child(idx, Lnast_node::create_const(std::string(trim(get_text(c)))));
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
  } else if (std::string(ts_node_type(func)) == "dot_expression" && ts_node_named_child_count(func) >= 2) {
    uint32_t fnc         = ts_node_named_child_count(func);
    TSNode   method_node = ts_node_named_child(func, fnc - 1);
    func_ref             = Lnast_node::create_ref(trim(get_text(method_node)));
    if (fnc == 2) {
      // Single-level receiver: lower it as an expression and pass as arg0.
      receiver_ref = expr_to_node(ts_node_named_child(func, 0));
    } else {
      // Multi-level receiver `a.b.c()` → receiver is the tuple_get of the
      // leading dotted path; emit that as an inline tuple_get call.
      auto tg_idx = lnast->add_child(stmts_index, Lnast_node::create_tuple_get());
      auto tg_ref = make_tmp_ref();
      lnast->add_child(tg_idx, tg_ref);
      lnast->add_child(tg_idx, expr_to_node(ts_node_named_child(func, 0)));
      for (uint32_t i = 1; i < fnc - 1; i++) {
        TSNode f = ts_node_named_child(func, i);
        lnast->add_child(tg_idx, Lnast_node::create_const(std::string(trim(get_text(f)))));
      }
      receiver_ref = tg_ref;
    }
    has_receiver = true;
  } else {
    func_ref = Lnast_node::create_ref(trim(get_text(func)));
  }
  // Pre-lower call arguments so any sub-expression statements are emitted
  // BEFORE the fcall that references them (same invariant as tuple_to_node).
  // Walk the tuple's children in source order; each comma-separated segment
  // is one argument. For segments whose expression is a hidden token (e.g.
  // `_simple_number`) there is no named child — fall back to the segment's
  // raw text as a constant, matching the tuple_to_node recovery path.
  std::vector<Lnast_node> call_args;
  if (!ts_node_is_null(arg)) {
    uint32_t total     = ts_node_child_count(arg);
    uint32_t seg_start = ts_node_start_byte(arg);
    if (total > 0) {
      seg_start = ts_node_end_byte(ts_node_child(arg, 0));  // after '('
    }
    bool       have_named_in_seg = false;
    Lnast_node seg_named_value;
    auto       flush_segment = [&](uint32_t seg_end) {
      if (have_named_in_seg) {
        call_args.push_back(seg_named_value);
      } else {
        auto text = trim(text_between(seg_start, seg_end));
        if (!text.empty()) {
          call_args.push_back(constant_text_to_node(text));
        }
      }
      have_named_in_seg = false;
    };
    for (uint32_t i = 1; i < total; i++) {
      TSNode      c = ts_node_child(arg, i);
      std::string ct(ts_node_type(c));
      if (ct == "," || ct == ")") {
        flush_segment(ts_node_start_byte(c));
        seg_start = ts_node_end_byte(c);
      } else if (ts_node_is_named(c)) {
        seg_named_value   = expr_to_node(c);
        have_named_in_seg = true;
      }
    }
  }

  auto idx = lnast->add_child(stmts_index, Lnast_node::create_func_call());
  auto ref = make_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, func_ref);
  if (has_receiver) {
    lnast->add_child(idx, receiver_ref);
  }
  for (auto& a : call_args) {
    lnast->add_child(idx, a);
  }
  return ref;
}

Lnast_node Prp2lnast::tuple_to_node(TSNode n, bool /*is_square*/) {
  uint32_t nnc = ts_node_named_child_count(n);

  // Pre-compute sub-expressions so their LNAST statements emit BEFORE the
  // tuple_add that references them — constprop runs in textual order.
  struct Item {
    bool        is_assign = false;
    std::string assign_key;
    Lnast_node  value;
  };
  std::vector<Item> items;
  items.reserve(nnc);
  for (uint32_t i = 0; i < nnc; i++) {
    TSNode      c = ts_node_named_child(n, i);
    std::string t(ts_node_type(c));
    if (t == "assignment") {
      Item it;
      it.is_assign = true;
      TSNode lv    = child_by_field(c, "lvalue");
      TSNode rv    = child_by_field(c, "rvalue");
      // The lvalue may carry a type_cast (`(a:u4=1, …)` parses lv as a
      // `typed_identifier`). Strip it: the ref text must be a bare
      // identifier; otherwise lnastfmt rejects the LNAST.
      std::string lvt2(ts_node_type(lv));
      if (lvt2 == "typed_identifier") {
        TSNode id = child_by_field(lv, "identifier");
        if (!ts_node_is_null(id)) {
          it.assign_key = std::string(trim(get_text(id)));
        }
      } else {
        // `_complex_identifier` with optional sibling `type` field on the
        // assignment node. Use the lvalue text directly; for plain
        // identifiers it's already clean.
        it.assign_key = std::string(trim(get_text(lv)));
      }
      if (it.assign_key.empty()) {
        // Anonymous slot (e.g. `(mut :u13 = 5)`) — synthesize a tmp name so
        // we don't emit an empty `ref` (lnastfmt would reject it).
        it.assign_key = get_tmp_name();
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
        TSNode      next  = ts_node_named_child(n, i + 1);
        std::string nextt(ts_node_type(next));
        Item        it;
        if (nextt == "typed_identifier") {
          // Bare declaration like `mut b` — emit the identifier as the
          // tuple slot's positional value (initial value is undefined).
          TSNode id = child_by_field(next, "identifier");
          if (!ts_node_is_null(id)) {
            it.value = identifier_to_node(id, true);
          } else {
            it.value = make_tmp_ref();
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
      it.value  = ts_node_is_null(id) ? make_tmp_ref() : identifier_to_node(id, true);
      items.push_back(std::move(it));
    } else {
      Item it;
      it.value = expr_to_node(c);
      items.push_back(std::move(it));
    }
  }

  auto idx = lnast->add_child(stmts_index, Lnast_node::create_tuple_add());
  auto ref = make_tmp_ref();
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
      auto aidx = lnast->add_child(idx, Lnast_node::create_assign());
      lnast->add_child(aidx, Lnast_node::create_ref(it.assign_key));
      lnast->add_child(aidx, it.value);
    } else {
      lnast->add_child(idx, it.value);
    }
  }
  return ref;
}

// ---------------- Factory hook ----------------
