// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "prp_string.hpp"
#include "prpparse/lexer.hpp"
#include "str_tools.hpp"

namespace prp_ast {
TSNode unwrap_expression(TSNode node) {
  while (ts_node_named_child_count(node) == 1) {
    const std::string_view kind = ts_node_type(node);
    if (kind != "expression_item" && kind != "expression_list" && kind != "constant" && kind != "paren_group") {
      break;
    }
    node = ts_node_named_child(node, 0);
  }
  return node;
}

std::optional<std::string> literal_string(TSNode node, std::string_view source) {
  node                        = unwrap_expression(node);
  const std::string_view kind = ts_node_type(node);
  if ((kind != "string_literal" && kind != "interpolated_string_literal") || ts_node_named_child_count(node)) {
    return std::nullopt;
  }
  const auto start = ts_node_start_byte(node);
  const auto end   = ts_node_end_byte(node);
  if (end < start + 2 || end > source.size()) {
    return std::nullopt;
  }
  auto body = source.substr(start + 1, end - start - 2);
  return kind == "string_literal" ? std::string(body) : str_tools::decode_cooked_text(body);
}

size_t skip_nested_block_comment(std::string_view s, size_t i) {
  int depth = 0;
  while (i + 1 < s.size()) {
    if (s[i] == '/' && s[i + 1] == '*') {
      ++depth;
      i += 2;
    } else if (s[i] == '*' && s[i + 1] == '/') {
      i += 2;
      if (--depth == 0) {
        return i;
      }
    } else {
      ++i;
    }
  }
  return s.size();
}

static std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return s;
}
// `s` is code (a hole's text): drop `//` line comments (to, not including, the
// newline) and NESTING `/* */` block comments, keep everything else verbatim.
// Only used on hole text outside the expression (the format-spec gap, an
// expression-less hole), which holds no strings or backtick names.
static std::string strip_code_comments(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t k = 0; k < s.size();) {
    if (k + 1 < s.size() && s[k] == '/' && s[k + 1] == '/') {
      while (k < s.size() && s[k] != '\n') {
        ++k;
      }
    } else if (k + 1 < s.size() && s[k] == '/' && s[k + 1] == '*') {
      k = skip_nested_block_comment(s, k);
    } else {
      out.push_back(s[k]);
      ++k;
    }
  }
  return out;
}

std::vector<Istring_piece> string_pieces(TSNode n, std::string_view source,
                                         const std::function<void(TSNode, std::string_view)>& bad_gap) {
  auto                       text_between = [&](uint32_t start, uint32_t end) { return source.substr(start, end - start); };
  std::vector<Istring_piece> pieces;
  const uint32_t             body_start = ts_node_start_byte(n) + 1;
  const uint32_t             body_end   = ts_node_end_byte(n) > body_start ? ts_node_end_byte(n) - 1 : body_start;

  std::vector<TSNode> exprs;  // the holes' expressions, in source order
  for (TSNode c : ts_node_named_children(n)) {
    if (std::string_view(ts_node_type(c)) != "comment") {
      exprs.push_back(c);
    }
  }
  size_t next_expr = 0;

  std::string lit;                     // decoded literal text not yet emitted
  uint32_t    seg       = body_start;  // start of the raw literal run not yet decoded
  auto        flush_raw = [&](uint32_t upto) {
    if (upto > seg) {
      lit += str_tools::decode_cooked_text(text_between(seg, upto));
    }
  };

  // Walk the body exactly like prpparse's Parser::parse_istring (the parser
  // that produced the hole expressions): an escape pair is text, `{{` is a
  // literal brace pair, and every other `{` opens a hole whose end the LEXER
  // finds (comments, nested strings and backtick names are skipped there).
  const prpparse::Lexer lex(*n.buf);
  uint32_t              i = body_start;
  while (i < body_end) {
    const char c = source[i];
    if (c == '\\') {
      // `\u{N}` carries its own braces: skip them whole (they open no hole).
      if (i + 2 < body_end && source[i + 1] == 'u' && source[i + 2] == '{') {
        uint32_t k = i + 3;
        while (k < body_end && source[k] != '}') {
          ++k;
        }
        i = k < body_end ? k + 1 : body_end;
        continue;
      }
      i += 2;
      continue;
    }
    if (c != '{') {
      ++i;
      continue;
    }
    if (i + 1 < body_end && source[i + 1] == '{') {
      i += 2;
      continue;
    }
    const uint32_t hole_end = lex.istring_hole_end(i);  // past the matching '}'
    const uint32_t close    = (hole_end > i + 1 && hole_end <= body_end) ? hole_end - 1 : body_end;
    while (next_expr < exprs.size() && ts_node_start_byte(exprs[next_expr]) <= i) {
      ++next_expr;  // defensive: an expression outside every hole
    }
    const bool has_expr = next_expr < exprs.size() && ts_node_start_byte(exprs[next_expr]) < close;
    flush_raw(i);
    if (!has_expr) {
      // No expression (`{}`, `{ }`, `{/* c */}`): the hole is literal text, and
      // a comment in it is a comment (removed), exactly like an empty hole.
      lit += "{";
      lit += strip_code_comments(text_between(i + 1, close));
      if (close < body_end) {
        lit += "}";
      }
    } else {
      TSNode expr = exprs[next_expr++];
      if (!lit.empty()) {
        pieces.push_back(Istring_piece{.text = std::move(lit)});
        lit.clear();
      }
      // Between the expression and the hole's `}`: only an optional `:spec`,
      // whitespace and comments (a `:` or `}` inside a comment is neither a
      // spec nor the hole's end).
      const std::string gap_s = strip_code_comments(text_between(ts_node_end_byte(expr), close));
      auto              gap   = trim(gap_s);
      std::string       spec;
      if (!gap.empty() && gap.front() == ':') {
        spec = std::string(trim(gap.substr(1)));
      } else if (!gap.empty()) {
        bad_gap(expr, gap);
      }
      while (next_expr < exprs.size() && ts_node_start_byte(exprs[next_expr]) < close) {
        ++next_expr;  // defensive: one expression per hole
      }
      pieces.push_back(Istring_piece{.expr = expr, .spec = std::move(spec), .is_hole = true});
    }
    i   = close < body_end ? close + 1 : body_end;
    seg = i;
  }
  flush_raw(body_end);
  if (!lit.empty() || pieces.empty()) {
    pieces.push_back(Istring_piece{.text = std::move(lit)});
  }
  return pieces;
}

}  // namespace prp_ast
