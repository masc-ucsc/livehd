// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "prp_ast_facade.hpp"

namespace prp_ast {
TSNode                     unwrap_expression(TSNode node);
// A raw or cooked string with no interpolation holes, as used by an import path or a static default.
std::optional<std::string> literal_string(TSNode node, std::string_view source);
size_t                     skip_nested_block_comment(std::string_view source, size_t offset);
// A double-quoted string body split into pieces: literal text (escapes
// decoded; a hole with no expression — `{}` or `{/* c */}` — stays literal
// text with its comments removed) or one `{expr[:spec]}` hole. The holes are
// found with prpparse's own hole scanner (Lexer::istring_hole_end), so a
// `}`/`{`/`:`/`/*` inside a comment, nested string or backtick name in a hole
// never ends it, opens another, or starts a format spec.
struct Istring_piece {
  std::string text;  // literal (when !is_hole)
  TSNode      expr{};
  std::string spec;  // format spec without the `:` (may be empty)
  bool        is_hole = false;
};
std::vector<Istring_piece> string_pieces(TSNode n, std::string_view source,
                                         const std::function<void(TSNode, std::string_view)>& bad_gap);
}  // namespace prp_ast
