//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// prp_ast_facade — a drop-in replacement for the tree-sitter C node API
// (`tree_sitter/api.h`) backed by prpparse's `Ast`. prp2lnast was written
// against tree-sitter's `TSNode` walk; this lets it traverse a prpparse parse
// tree unchanged. See ../tree-sitter-pyrope/prpparse/plan.md (Phase B).
//
// prpparse omits anonymous tokens, EXCEPT a few that carry a field prp2lnast
// reads (arg-list `mod` = ref/reg/..., a match-arm condition operator). Those
// are emitted as marker nodes flagged `named = false`, so the named/all-child
// distinction below reproduces tree-sitter's behaviour: `named_child` skips
// them, `child` includes them, `is_named` reports false.

#include <cstdint>
#include <string_view>

#include "prpparse/ast.hpp"
#include "prpparse/node_kind.hpp"
#include "prpparse/source_buffer.hpp"

// tree-sitter value types reproduced 1:1 (so prp2lnast's by-value usage holds).
struct TSPoint {
  uint32_t row;
  uint32_t column;
};

// A node handle = the Ast node + the source buffer (for line/col + text). A
// default-constructed node is "null" (mirrors tree-sitter's null node).
struct TSNode {
  const prpparse::Ast*           a   = nullptr;
  const prpparse::Source_buffer* buf = nullptr;
};

inline bool ts_node_is_null(TSNode n) { return n.a == nullptr; }
inline bool ts_node_is_named(TSNode n) { return n.a != nullptr && n.a->named; }
inline bool ts_node_is_missing(TSNode /*n*/) { return false; }  // fail-fast parser: no MISSING nodes
inline bool ts_node_has_error(TSNode /*n*/) { return false; }   // fail-fast parser: no ERROR nodes
inline bool ts_node_eq(TSNode x, TSNode y) { return x.a == y.a; }

inline const char* ts_node_type(TSNode n) {
  if (n.a == nullptr) {
    return "";
  }
  // kind_name returns a string_view over a static string literal (NUL-terminated).
  return prpparse::kind_name(n.a->kind).data();
}

inline uint32_t ts_node_start_byte(TSNode n) { return n.a != nullptr ? n.a->start_byte : 0; }
inline uint32_t ts_node_end_byte(TSNode n) { return n.a != nullptr ? n.a->end_byte : 0; }

inline TSPoint ts_node_start_point(TSNode n) {
  if (n.a == nullptr || n.buf == nullptr) {
    return TSPoint{0, 0};
  }
  return TSPoint{n.buf->line_of(n.a->start_byte) - 1, n.buf->col_of(n.a->start_byte) - 1};
}
inline TSPoint ts_node_end_point(TSNode n) {
  if (n.a == nullptr || n.buf == nullptr) {
    return TSPoint{0, 0};
  }
  return TSPoint{n.buf->line_of(n.a->end_byte) - 1, n.buf->col_of(n.a->end_byte) - 1};
}

// ---- children (all, incl. anonymous markers) -------------------------------
inline uint32_t ts_node_child_count(TSNode n) {
  return n.a != nullptr ? static_cast<uint32_t>(n.a->kids.size()) : 0;
}
inline TSNode ts_node_child(TSNode n, uint32_t i) {
  if (n.a == nullptr || i >= n.a->kids.size()) {
    return TSNode{};
  }
  return TSNode{n.a->kids[i], n.buf};
}

// ---- named children (anonymous markers skipped, like tree-sitter) ----------
inline uint32_t ts_node_named_child_count(TSNode n) {
  if (n.a == nullptr) {
    return 0;
  }
  uint32_t c = 0;
  for (const prpparse::Ast* k : n.a->kids) {
    if (k->named) {
      ++c;
    }
  }
  return c;
}
inline TSNode ts_node_named_child(TSNode n, uint32_t i) {
  if (n.a == nullptr) {
    return TSNode{};
  }
  uint32_t c = 0;
  for (const prpparse::Ast* k : n.a->kids) {
    if (!k->named) {
      continue;
    }
    if (c == i) {
      return TSNode{k, n.buf};
    }
    ++c;
  }
  return TSNode{};
}

// Field role of child `i` (NUL-terminated; nullptr when the child has no field,
// matching tree-sitter's ts_node_field_name_for_child).
inline const char* ts_node_field_name_for_child(TSNode n, uint32_t i) {
  if (n.a == nullptr || i >= n.a->kids.size()) {
    return nullptr;
  }
  const prpparse::Field f = n.a->kids[i]->field;
  if (f == prpparse::Field::none) {
    return nullptr;
  }
  return prpparse::field_name(f).data();
}

// First child carrying field `name` (length `len`), or a null node.
inline TSNode ts_node_child_by_field_name(TSNode n, const char* name, uint32_t len) {
  if (n.a == nullptr) {
    return TSNode{};
  }
  const std::string_view want(name, len);
  for (const prpparse::Ast* k : n.a->kids) {
    if (k->field == prpparse::Field::none) {
      continue;
    }
    if (prpparse::field_name(k->field) == want) {
      return TSNode{k, n.buf};
    }
  }
  return TSNode{};
}

inline TSNode ts_node_parent(TSNode n) {
  if (n.a == nullptr || n.a->parent == nullptr) {
    return TSNode{};
  }
  return TSNode{n.a->parent, n.buf};
}

inline TSNode ts_node_next_named_sibling(TSNode n) {
  if (n.a == nullptr || n.a->parent == nullptr) {
    return TSNode{};
  }
  bool found = false;
  for (const prpparse::Ast* k : n.a->parent->kids) {
    if (found && k->named) {
      return TSNode{k, n.buf};
    }
    if (k == n.a) {
      found = true;
    }
  }
  return TSNode{};
}
