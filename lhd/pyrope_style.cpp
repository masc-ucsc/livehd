// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pyrope_style.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "tree_sitter/api.h"

extern "C" const TSLanguage* tree_sitter_pyrope();

namespace livehd::pyrope::style {
namespace {

bool digit(char c) { return c >= '0' && c <= '9'; }

// Leave signed bit patterns, unknown bits, and huge constants exact. Bounded
// values make the difference-of-differences check below overflow-free.
std::optional<int64_t> number(std::string_view text) {
  std::string clean;
  for (char c : text) {
    if (c != '_') {
      clean += c;
    }
  }
  std::string_view s        = clean;
  bool             negative = s.starts_with('-');
  if (negative) {
    s.remove_prefix(1);
  }
  int base = 10;
  if (s.starts_with("0x") || s.starts_with("0X")) {
    base = 16;
    s.remove_prefix(2);
  } else if (s.starts_with("0o") || s.starts_with("0O")) {
    base = 8;
    s.remove_prefix(2);
  } else if (s.starts_with("0d") || s.starts_with("0D")) {
    s.remove_prefix(2);
  }
  int64_t v      = 0;
  auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
  if (ec != std::errc{} || end != s.data() + s.size() || v > std::numeric_limits<int64_t>::max() / 4) {
    return std::nullopt;
  }
  return negative ? -v : v;
}

struct Atom {
  int64_t  value;
  uint32_t start, end;
};

struct Statement {
  TSNode            node;
  TSNode            start_node;
  size_t            shape;
  size_t            tokens;
  std::vector<Atom> atoms;
};

Range range(TSNode first, TSNode last) {
  auto a = ts_node_start_point(first);
  auto b = ts_node_end_point(last);
  return {ts_node_start_byte(first), ts_node_end_byte(last), a.row + 1, a.column + 1, b.row + 1, b.column + 1};
}

// Cursor iteration is linear in the number of children, even for enormous
// generated scopes. Keep anonymous operator/keyword tokens, not just names.
template <typename Fn>
void children(TSNode node, Fn&& fn) {
  auto cursor = ts_tree_cursor_new(node);
  if (ts_tree_cursor_goto_first_child(&cursor)) {
    do {
      fn(ts_tree_cursor_current_node(&cursor), ts_tree_cursor_current_field_name(&cursor));
    } while (ts_tree_cursor_goto_next_sibling(&cursor));
  }
  ts_tree_cursor_delete(&cursor);
}

class Detector {
  std::string_view                        source;
  const Options&                          options;
  std::unordered_map<std::string, size_t> shapes;
  std::vector<Finding>                    candidates;
  std::vector<Range>                      parse_errors;

  std::string_view text(TSNode n) const {
    return source.substr(ts_node_start_byte(n), ts_node_end_byte(n) - ts_node_start_byte(n));
  }

  void fingerprint(TSNode n, std::string& shape, Statement& stmt, bool type = false) {
    std::string_view kind = ts_node_type(n);
    if (kind == "comment" || kind == ";") {
      return;
    }
    type   = type || kind == "type_cast" || kind.ends_with("_type");
    shape += '(';
    shape += kind;
    shape += ':';
    if (kind == "string_literal" || kind == "interpolated_string_literal") {
      ++stmt.tokens;
      shape += text(n);
    } else if (ts_node_child_count(n) == 0) {
      ++stmt.tokens;
      auto s = text(n);
      if (!type && kind == "integer_literal") {
        if (auto v = number(s)) {
          shape += "{number}";
          stmt.atoms.push_back({*v, ts_node_start_byte(n), ts_node_end_byte(n)});
        } else {
          shape += s;
        }
      } else if (!type && kind == "identifier") {
        for (size_t i = 0; i < s.size();) {
          size_t j = i;
          if (digit(s[i])) {
            while (j < s.size() && digit(s[j])) {
              ++j;
            }
            if (auto v = number(s.substr(i, j - i))) {
              shape += "{index}";
              stmt.atoms.push_back(
                  {*v, ts_node_start_byte(n) + static_cast<uint32_t>(i), ts_node_start_byte(n) + static_cast<uint32_t>(j)});
            } else {
              shape += s.substr(i, j - i);
            }
          } else {
            shape += s[i];
            ++j;
          }
          i = j;
        }
      } else {
        shape += s;
      }
    } else {
      children(n, [&](TSNode c, const char* field) {
        if (field) {
          shape += field;
          shape += '=';
        }
        fingerprint(c, shape, stmt, type);
      });
    }
    shape += ')';
  }

  static bool same_progression(const Statement& a, const Statement& b, const Statement& c) {
    if (a.shape != b.shape || b.shape != c.shape) {
      return false;
    }
    for (size_t k = 0; k < a.atoms.size(); ++k) {
      if (b.atoms[k].value - a.atoms[k].value != c.atoms[k].value - b.atoms[k].value) {
        return false;
      }
    }
    return true;
  }

  void candidate(const std::vector<Statement>& stmts, size_t start, size_t period, size_t copies) {
    const auto&                                   first = stmts[start];
    Finding                                       f{range(first.start_node, stmts[start + period * copies - 1].node),
                                                    range(first.start_node, stmts[start + period - 1].node),
                                                    period,
                                                    copies,
                                                    0,
                                                    false,
                                                    {},
                                                    {}};
    uint32_t                                      pos = f.first_copy.start_byte;
    // Each distinct affine progression gets a placeholder, shared across all
    // statements of the template. Fixed values retain their original spelling.
    std::map<std::pair<int64_t, int64_t>, size_t> variables;
    for (size_t j = 0; j < period; ++j) {
      const auto& a  = stmts[start + j];
      const auto& b  = stmts[start + period + j];
      f.score       += a.tokens * (copies - 1);
      for (size_t k = 0; k < a.atoms.size(); ++k) {
        auto atom = a.atoms[k];
        auto step = b.atoms[k].value - atom.value;
        if (step == 0) {
          continue;
        }
        f.progressing       = true;
        auto [it, inserted] = variables.try_emplace({atom.value, step}, variables.size());
        if (inserted && it->second < 8) {
          if (!f.progression.empty()) {
            f.progression += "; ";
          }
          f.progression += std::format("p{} = {} + ({} * i)", it->second, atom.value, step);
        }
        if (f.pattern.size() < 1200) {
          f.pattern += source.substr(pos, atom.start - pos);
          f.pattern += std::format("{{p{}}}", it->second);
        }
        pos = atom.end;
      }
    }
    if (f.pattern.size() < 1200) {
      f.pattern += source.substr(pos, f.first_copy.end_byte - pos);
    }
    if (f.pattern.size() > 1200) {
      f.pattern.resize(1200);
      f.pattern += " ... [template truncated; see first-copy location]";
    }
    if (variables.size() > 8) {
      f.progression += std::format("; {} more progressions", variables.size() - 8);
    }
    candidates.push_back(std::move(f));
  }

  void sequence(const std::vector<Statement>& stmts) {
    // A run of matching triples at distance p identifies tandem copies of a
    // p-statement block. O(max_block_statements * source tokens), without an
    // all-pairs comparison or enumerating every subrange of a repeated run.
    const size_t limit = std::min(options.max_block_statements, stmts.size() / options.min_repeats);
    for (size_t p = 1; p <= limit; ++p) {
      size_t start = 0;
      for (size_t i = 0; i <= stmts.size() - 2 * p; ++i) {
        if (i < stmts.size() - 2 * p && same_progression(stmts[i], stmts[i + p], stmts[i + 2 * p])) {
          continue;
        }
        const size_t copies = (i - start) / p + 2;
        if (copies >= options.min_repeats) {
          candidate(stmts, start, p, copies);
        }
        start = i + 1;
      }
    }
  }

  void visit(TSNode node) {
    std::string_view kind = ts_node_type(node);
    if (kind == "ERROR" || ts_node_is_missing(node)) {
      if (parse_errors.size() < 8) {
        parse_errors.push_back(range(node, node));
      }
      return;
    }
    if (kind == "scope_statement" || kind == "description") {
      std::vector<Statement> stmts;
      std::string            prefix;
      TSNode                 prefix_node{};
      children(node, [&](TSNode c, const char* field) {
        std::string_view ck = ts_node_type(c);
        if (ck == "comment") {
          return;
        }
        if (!ts_node_is_named(c)) {
          // wrap/sat live beside assignment nodes in the grammar. Preserve
          // these modifiers so narrowing policies never match one another.
          if (ck == "wrap" || ck == "sat") {
            prefix_node  = c;
            prefix      += ck;
          }
          return;
        }
        if (ts_node_has_error(c) || ts_node_is_missing(c) || ck == "ERROR" || (field && std::string_view(field) == "attributes")) {
          sequence(stmts);
          stmts.clear();
          prefix.clear();
          prefix_node = {};
          return;
        }
        Statement   s{c, ts_node_is_null(prefix_node) ? c : prefix_node, 0, 0, {}};
        std::string shape = std::move(prefix);
        prefix.clear();
        prefix_node = {};
        fingerprint(c, shape, s);
        auto [it, inserted] = shapes.try_emplace(std::move(shape), shapes.size());
        (void)inserted;
        s.shape = it->second;
        stmts.push_back(std::move(s));
      });
      sequence(stmts);
    }
    children(node, [&](TSNode c, const char*) {
      if ((ts_node_is_named(c) || ts_node_is_missing(c)) && std::string_view(ts_node_type(c)) != "comment") {
        visit(c);
      }
    });
  }

public:
  Detector(std::string_view src, const Options& opts) : source(src), options(opts) {}

  Report run(TSNode root) {
    visit(root);
    std::sort(candidates.begin(), candidates.end(), [](const Finding& a, const Finding& b) {
      if (a.score != b.score) {
        return a.score > b.score;
      }
      if (a.statements != b.statements) {
        return a.statements < b.statements;
      }
      return a.range.start_byte < b.range.start_byte;
    });
    Report report;
    report.partial      = ts_node_has_error(root);
    report.parse_errors = std::move(parse_errors);
    std::map<uint32_t, uint32_t> occupied;
    for (auto& f : candidates) {
      auto next = occupied.lower_bound(f.range.start_byte);
      if ((next != occupied.end() && next->first < f.range.end_byte)
          || (next != occupied.begin() && std::prev(next)->second > f.range.start_byte)) {
        continue;
      }
      occupied.emplace(f.range.start_byte, f.range.end_byte);
      ++report.total_findings;
      if (report.findings.size() < options.max_findings) {
        report.findings.push_back(std::move(f));
      }
    }
    return report;
  }
};

}  // namespace

Report analyze(std::string_view source, const Options& options) {
  if (options.min_repeats < 3 || options.max_block_statements == 0 || options.max_findings == 0) {
    throw std::invalid_argument("style requires at least three repetitions and positive limits");
  }
  if (source.size() > std::numeric_limits<uint32_t>::max()) {
    throw std::runtime_error("source exceeds Tree-sitter's 32-bit byte limit");
  }
  std::unique_ptr<TSParser, decltype(&ts_parser_delete)> parser(ts_parser_new(), ts_parser_delete);
  if (!parser || !ts_parser_set_language(parser.get(), tree_sitter_pyrope())) {
    throw std::runtime_error("cannot initialize the Pyrope Tree-sitter parser");
  }
  std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree(
      ts_parser_parse_string(parser.get(), nullptr, source.data(), static_cast<uint32_t>(source.size())),
      ts_tree_delete);
  if (!tree) {
    throw std::runtime_error("Tree-sitter could not parse the source");
  }
  return Detector(source, options).run(ts_tree_root_node(tree.get()));
}

}  // namespace livehd::pyrope::style
