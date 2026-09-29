// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pyrope_style.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
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
  std::unordered_set<uint32_t>            io_identifiers;

  std::string_view text(TSNode n) const {
    return source.substr(ts_node_start_byte(n), ts_node_end_byte(n) - ts_node_start_byte(n));
  }

  // Mark IO occurrences before comparing subtrees, including whole lambdas.
  // Keep enclosing IOs visible to nested lambdas, but never leak names between
  // sibling modules. Only parameter bindings count, not types or defaults.
  void track_ios(TSNode node, std::unordered_map<std::string_view, size_t>& names, TSNode signature = {}) {
    if (ts_node_is_null(signature)) {
      signature = node;
    }
    std::vector<std::string_view> declared;
    if (std::string_view(ts_node_type(signature)) == "lambda") {
      auto binding = [&](TSNode arg) {
        if (std::string_view(ts_node_type(arg)) == "typed_identifier") {
          auto id = ts_node_child_by_field_name(arg, "identifier", 10);
          if (!ts_node_is_null(id)) {
            auto name = text(id);
            declared.push_back(name);
            ++names[name];
          }
        }
      };
      children(signature, [&](TSNode child, const char*) {
        if (std::string_view(ts_node_type(child)) != "function_definition_decl") {
          return;
        }
        children(child, [&](TSNode args, const char* field) {
          if (!field || (std::string_view(field) != "input" && std::string_view(field) != "output")) {
            return;
          }
          if (std::string_view(ts_node_type(args)) == "arg_list") {
            children(args, [&](TSNode arg, const char* arg_field) {
              if (!arg_field || std::string_view(arg_field) != "definition") {
                binding(arg);
              }
            });
          } else {
            binding(args);
          }
        });
      });
    }
    if (std::string_view(ts_node_type(node)) == "identifier" && names.contains(text(node))) {
      io_identifiers.insert(ts_node_start_byte(node));
    }
    TSNode previous{};
    children(node, [&](TSNode child, const char*) {
      auto kind = std::string_view(ts_node_type(child));
      if (kind == "comment") {
        return;
      }
      // Tree-sitter can end a lambda at its signature and parse its body as
      // the next sibling (e.g. after an import). Treat that adjacent scope as
      // its body too. A semicolon or any intervening statement breaks the link.
      TSNode body_signature{};
      if (kind == "scope_statement" && !ts_node_is_null(previous) && std::string_view(ts_node_type(previous)) == "lambda"
          && ts_node_is_null(ts_node_child_by_field_name(previous, "code", 4))) {
        body_signature = previous;
      }
      track_ios(child, names, body_signature);
      previous = child;
    });
    for (auto name : declared) {
      if (--names.at(name) == 0) {
        names.erase(name);
      }
    }
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
      } else if (!type && kind == "identifier" && !io_identifiers.contains(ts_node_start_byte(n))) {
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
    const auto& first = stmts[start];
    Finding     f{};
    f.range                                           = range(first.start_node, stmts[start + period * copies - 1].node);
    f.first_copy                                      = range(first.start_node, stmts[start + period - 1].node);
    f.statements                                      = period;
    f.repetitions                                     = copies;
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
    f.rule = f.progressing ? Rule::LikelyUnrolledLoop : Rule::RepeatedCode;
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

  static TSNode field(TSNode node, std::string_view name) {
    return ts_node_child_by_field_name(node, name.data(), static_cast<uint32_t>(name.size()));
  }

  using Path = std::vector<std::string>;

  // Only static, unescaped field paths. In particular, a literal identifier
  // `a.b` is not a selection of field b from a, and a[i].b is not static.
  bool path(TSNode node, Path& result) const {
    const auto kind = std::string_view(ts_node_type(node));
    if (kind == "identifier") {
      const auto name = text(node);
      if (name.starts_with('`')) {
        return false;
      }
      result.emplace_back(name);
      return true;
    }
    if (kind != "dot_expression" && kind != "typed_identifier") {
      return false;
    }
    bool valid = true;
    children(node, [&](TSNode child, const char*) {
      const auto ck = std::string_view(ts_node_type(child));
      if (ck == "comment" || ck == ".") {
        return;
      }
      valid = path(child, result) && valid;
    });
    return valid && !result.empty();
  }

  static std::string spelling(const Path& parts, size_t count) {
    std::string result;
    for (size_t i = 0; i < count; ++i) {
      if (i) {
        result += '.';
      }
      result += parts[i];
    }
    return result;
  }

  static bool prefix(const Path& a, const Path& b) { return a.size() <= b.size() && std::equal(a.begin(), a.end(), b.begin()); }

  static size_t common_prefix(const Path& a, const Path& b, size_t limit) {
    size_t n = 0;
    while (n < limit && n < b.size() && a[n] == b[n]) {
      ++n;
    }
    return n;
  }

  static bool same_suffix(const Path& a, size_t ai, const Path& b, size_t bi) {
    return a.size() - ai == b.size() - bi && std::equal(a.begin() + ai, a.end(), b.begin() + bi);
  }

  static size_t tokens(TSNode node) {
    if (std::string_view(ts_node_type(node)) == "comment") {
      return 0;
    }
    if (!ts_node_child_count(node)) {
      return 1;
    }
    size_t count = 0;
    children(node, [&](TSNode c, const char*) { count += tokens(c); });
    return count;
  }

  static void related(Finding& f, TSNode node, std::string message) {
    // Keep diagnostics bounded even for enormous generated scopes.
    if (f.related.size() < 8) {
      f.related.push_back({range(node, node), std::move(message)});
    }
  }

  bool plain_assignment(TSNode node, Path& destination) const {
    if (std::string_view(ts_node_type(node)) != "assignment" || !ts_node_is_null(field(node, "decl"))
        || !ts_node_is_null(field(node, "type"))) {
      return false;
    }
    const auto op = field(node, "operator");
    return !ts_node_is_null(op) && text(op) == "=" && path(field(node, "lvalue"), destination);
  }

  struct Copy {
    TSNode node;
    Path   destination, source;
  };

  void tuple_copies(const std::vector<Statement>& stmts) {
    std::vector<Copy> run;
    std::set<Path>    destinations;
    size_t            dp = 0, sp = 0;
    auto              flush = [&] {
      if (run.size() >= 2) {
        Finding f{};
        f.rule         = Rule::WholeTupleCopy;
        f.range        = range(run.front().node, run.back().node);
        const auto dst = spelling(run.front().destination, dp);
        const auto src = spelling(run.front().source, sp);
        f.message      = std::format("{} matching field copies from '{}' to '{}'", run.size(), src, dst);
        f.hint         = std::format(
            "consider '{} = {}' only if these fields form the complete compatible bundle; "
            "check assignment semantics and LEC the collapsed copy against the original",
            dst,
            src);
        f.attributes = {
            {"destination",                        dst},
            {     "source",                        src},
            {"field_count", std::to_string(run.size())}
        };
        // Each additional copy repeats the two bundle paths and an assignment.
        f.score = (run.size() - 1) * (2 * (dp + sp) + 1);
        for (const auto& copy : run) {
          related(f, copy.node, "matching field copy");
        }
        candidates.push_back(std::move(f));
      }
      run.clear();
      destinations.clear();
    };
    for (const auto& stmt : stmts) {
      Copy copy{stmt.node, {}, {}};
      if (!ts_node_eq(stmt.start_node, stmt.node) || !plain_assignment(stmt.node, copy.destination)
          || !path(field(stmt.node, "rvalue"), copy.source) || copy.destination.size() < 2 || copy.source.size() < 2
          || copy.destination.front() == copy.source.front() || copy.destination.back() != copy.source.back()) {
        flush();
        continue;
      }
      if (!run.empty()) {
        const auto& first   = run.front();
        // Grow a contiguous run, broadening both bundle prefixes together only
        // when all the previously matching suffixes still match.
        const auto  drop    = std::max(dp - common_prefix(first.destination, copy.destination, dp),
                                       sp - common_prefix(first.source, copy.source, sp));
        const auto  next    = destinations.lower_bound(copy.destination);
        const bool  overlap = (next != destinations.end() && prefix(copy.destination, *next))
                              || (next != destinations.begin() && prefix(*std::prev(next), copy.destination));
        if (drop >= dp || drop >= sp || overlap || !same_suffix(first.destination, dp - drop, first.source, sp - drop)
            || !same_suffix(copy.destination, dp - drop, copy.source, sp - drop)) {
          flush();
        } else {
          dp -= drop;
          sp -= drop;
        }
      }
      if (run.empty()) {
        dp = copy.destination.size() - 1;
        sp = copy.source.size() - 1;
      }
      destinations.insert(copy.destination);
      run.push_back(std::move(copy));
    }
    flush();
  }

  void bundle_arguments(TSNode node) {
    if (std::string_view(ts_node_type(node)) != "function_call_expression") {
      return;
    }
    const auto args = field(node, "argument");
    if (ts_node_is_null(args) || std::string_view(ts_node_type(args)) != "arg_tuple") {
      return;
    }
    std::map<std::string, std::vector<TSNode>> groups;
    children(args, [&](TSNode arg, const char*) {
      if (std::string_view(ts_node_type(arg)) != "arg_assignment") {
        return;
      }
      const auto lhs = field(arg, "lvalue");
      if (ts_node_is_null(lhs) || std::string_view(ts_node_type(lhs)) != "identifier") {
        return;
      }
      auto name = text(lhs);
      if (name.size() < 3 || !name.starts_with('`') || !name.ends_with('`')) {
        return;
      }
      name           = name.substr(1, name.size() - 2);
      const auto dot = name.find('.');
      if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size()) {
        return;
      }
      // Dotted generated paths only, not arbitrary escaped Verilog names.
      bool segment_start = true;
      for (char c : name) {
        if (c == '.') {
          if (segment_start) {
            return;
          }
          segment_start = true;
        } else {
          const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
          if (!letter && (segment_start || (!digit(c) && c != '$'))) {
            return;
          }
          segment_start = false;
        }
      }
      groups[std::string(name.substr(0, dot))].push_back(arg);
    });
    for (const auto& [bundle, args_in_group] : groups) {
      Finding f{};
      f.rule    = Rule::FlattenedBundleArguments;
      // Anchor on the first contributing argument: separate groups in the
      // same call must not suppress one another even if they interleave.
      f.range   = range(args_in_group.front(), args_in_group.front());
      f.message = std::format("{} flattened named arguments for bundle '{}'", args_in_group.size(), bundle);
      f.hint    = std::format(
          "consider a structured '{}' argument; reuse an existing bundle or construct a named tuple, "
          "and update the callee interface and its callers together",
          bundle);
      f.attributes = {
          {        "bundle",                               bundle},
          {"argument_count", std::to_string(args_in_group.size())}
      };
      for (auto arg : args_in_group) {
        f.score += tokens(field(arg, "lvalue")) + 2;
        related(f, arg, "flattened bundle argument");
      }
      candidates.push_back(std::move(f));
    }
  }

  static bool pure_value(TSNode node) {
    const auto kind = std::string_view(ts_node_type(node));
    if (kind == "function_call_expression" || kind == "lambda" || kind == "assignment" || kind == "arg_assignment"
        || kind == "ref_identifier" || kind == "if_expression" || kind == "match_expression" || kind == "scope_statement") {
      return false;
    }
    bool pure = true;
    children(node, [&](TSNode c, const char*) { pure = pure_value(c) && pure; });
    return pure;
  }

  void conditional(TSNode node) {
    if (std::string_view(ts_node_type(node)) != "if_expression") {
      return;
    }
    bool                valid = true, has_else = false;
    std::vector<TSNode> assignments;
    Path                destination;
    children(node, [&](TSNode child, const char* name) {
      const auto kind = std::string_view(ts_node_type(child));
      if (kind == "unique" || (name && std::string_view(name) == "init")) {
        valid = false;
      }
      if (kind == "else") {
        has_else = true;
      }
      if (kind != "scope_statement") {
        return;
      }
      TSNode assignment{};
      children(child, [&](TSNode statement, const char*) {
        const auto sk = std::string_view(ts_node_type(statement));
        if (sk == "comment" || sk == "{" || sk == "}" || sk == ";") {
          return;
        }
        if (!ts_node_is_null(assignment) || sk != "assignment") {
          valid = false;
        }
        assignment = statement;
      });
      Path lhs;
      if (ts_node_is_null(assignment) || !plain_assignment(assignment, lhs) || !pure_value(field(assignment, "rvalue"))) {
        valid = false;
        return;
      }
      if (assignments.empty()) {
        destination = lhs;
      } else if (lhs != destination) {
        valid = false;
      }
      assignments.push_back(assignment);
    });
    if (!valid || !has_else || assignments.size() < 2) {
      return;
    }
    const auto dst = spelling(destination, destination.size());
    Finding    f{};
    f.rule    = Rule::SingleDestinationConditional;
    f.range   = range(node, node);
    f.message = std::format("{} exhaustive branches each assign '{}'", assignments.size(), dst);
    f.hint    = std::format(
        "consider '{} = if ... {{ ... }} elif ... {{ ... }} else {{ ... }}'; "
        "preserve the existing branch order and branch values",
        dst);
    f.attributes = {
        { "destination",                                dst},
        {"branch_count", std::to_string(assignments.size())}
    };
    f.score = (assignments.size() - 1) * (2 * destination.size());
    for (auto assignment : assignments) {
      related(f, assignment, "assignment to the same destination");
    }
    candidates.push_back(std::move(f));
  }

  void visit(TSNode node) {
    std::string_view kind = ts_node_type(node);
    if (kind == "ERROR" || ts_node_is_missing(node)) {
      if (parse_errors.size() < 8) {
        parse_errors.push_back(range(node, node));
      }
      return;
    }
    if (!ts_node_has_error(node)) {
      bundle_arguments(node);
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
          tuple_copies(stmts);
          stmts.clear();
          prefix.clear();
          prefix_node = {};
          return;
        }
        if (prefix.empty() && !ts_node_has_error(c)) {
          conditional(c);
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
      tuple_copies(stmts);
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
    std::unordered_map<std::string_view, size_t> io_names;
    track_ios(root, io_names);
    visit(root);
    std::sort(candidates.begin(), candidates.end(), [](const Finding& a, const Finding& b) {
      if (a.score != b.score) {
        return a.score > b.score;
      }
      if (a.statements != b.statements) {
        return a.statements < b.statements;
      }
      if (a.range.start_byte != b.range.start_byte) {
        return a.range.start_byte < b.range.start_byte;
      }
      return a.rule < b.rule;
    });
    Report report;
    report.partial      = ts_node_has_error(root);
    report.parse_errors = std::move(parse_errors);
    std::map<Rule, std::map<uint32_t, uint32_t>> occupied_by_rule;
    for (auto& f : candidates) {
      // The two repetition codes are one detector and retain their shared
      // overlap suppression; independent recommendations may overlap.
      const auto family   = f.rule == Rule::LikelyUnrolledLoop ? Rule::RepeatedCode : f.rule;
      auto&      occupied = occupied_by_rule[family];
      auto       next     = occupied.lower_bound(f.range.start_byte);
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

std::string_view rule_name(Rule rule) {
  switch (rule) {
    case Rule::RepeatedCode                : return "repeated-code";
    case Rule::LikelyUnrolledLoop          : return "likely-unrolled-loop";
    case Rule::WholeTupleCopy              : return "whole-tuple-copy";
    case Rule::FlattenedBundleArguments    : return "flattened-bundle-arguments";
    case Rule::SingleDestinationConditional: return "single-destination-conditional";
  }
  throw std::invalid_argument("unknown style rule");
}

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
