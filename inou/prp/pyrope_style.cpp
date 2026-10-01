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

  // `// prp-style-allow code-a, code-b` silences those rule codes from the end
  // of the comment to the end of the tree node that holds it (its scope). The
  // traversal pops that scope when it leaves the node, so the allow ends there.
  struct Allow {
    uint32_t                 start, end;
    std::vector<std::string> codes;
  };
  std::vector<Allow> allows;
  size_t             suppressed = 0;

  static bool blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

  void directive(TSNode comment, TSNode scope) {
    auto body = text(comment);
    if (body.starts_with("//")) {
      body.remove_prefix(2);
    } else if (body.starts_with("/*") && body.ends_with("*/") && body.size() >= 4) {
      body = body.substr(2, body.size() - 4);
    } else {
      return;
    }
    constexpr std::string_view tag = "prp-style-allow";
    while (!body.empty() && blank(body.front())) {
      body.remove_prefix(1);
    }
    if (!body.starts_with(tag)) {
      return;
    }
    body.remove_prefix(tag.size());
    if (!body.empty() && !blank(body.front())) {
      return;  // e.g. `prp-style-allowed`
    }
    Allow allow{ts_node_end_byte(comment), ts_node_end_byte(scope), {}};
    for (;;) {
      while (!body.empty() && (body.front() == ',' || blank(body.front()))) {
        body.remove_prefix(1);
      }
      if (body.empty()) {
        break;
      }
      const auto n = body.find_first_of(", \t\r\n");
      allow.codes.emplace_back(body.substr(0, n));
      body.remove_prefix(n == std::string_view::npos ? body.size() : n);
    }
    if (!allow.codes.empty()) {
      allows.push_back(std::move(allow));
    }
  }

  bool allowed(const Finding& f) const {
    const auto code = rule_name(f.rule);
    for (const auto& a : allows) {
      if (f.range.start_byte >= a.start && f.range.start_byte < a.end
          && std::find(a.codes.begin(), a.codes.end(), code) != a.codes.end()) {
        return true;
      }
    }
    return false;
  }

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
    if (ts_node_is_null(node)) {
      return node;
    }
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

  // ---- reset rules -------------------------------------------------------
  // A module input that only ever acts as a reset should be (1) a structural
  // `reset_pin` of the registers it clears and (2) typed `Reset`, whatever its
  // name. "Only acts as a reset" is functional: every use of the port is either
  // the condition of a hand-written `if PORT { reg = CONST }` clear or a
  // `reset_pin=PORT` attribute; any other use (arithmetic, data, an argument)
  // disqualifies it.
  struct Port {
    TSNode      node;
    std::string type;  // "U1", "Bool" or "Reset"
    size_t      total = 0, hardcoded = 0, structural = 0;
  };
  struct RegInfo {
    std::string type;  // declared type text; empty when untyped
    bool        has_attributes = false;
    // Arrays (a memory) and latches are not flop resets: clearing a whole array
    // in one cycle changes the hardware, and a latch has no clock for a sync reset.
    bool        eligible = true;
    // A register that already has a reset (`reset_pin=`, or any initial value
    // other than `nil`, which binds the implicit reset) is not "missing" one: a
    // later conditional write to it is ordinary next-state logic.
    bool        has_reset = false;
  };
  struct ResetIf {
    TSNode                        node;
    std::string_view              port, value;
    bool                          active_high;
    std::vector<std::string_view> regs;
    std::vector<TSNode>           assigns;
    size_t                        removable;
  };

  template <typename Fn>
  static void descend(TSNode n, Fn&& fn) {  // pre-order over named nodes; fn returns false to prune
    if (!fn(n)) {
      return;
    }
    children(n, [&](TSNode c, const char*) {
      if (ts_node_is_named(c)) {
        descend(c, fn);
      }
    });
  }

  static std::string_view kind_of(TSNode n) { return ts_node_is_null(n) ? std::string_view{} : std::string_view(ts_node_type(n)); }

  // `X`, `(X)`, `!X`, `not X`, `~X`, `X == 0|1`, `X != 0|1` (also true/false).
  bool reset_condition(TSNode cond, std::string_view& port, bool& active_high) const {
    if (ts_node_is_null(cond)) {
      return false;
    }
    const auto k = kind_of(cond);
    if (k == "identifier") {
      port        = text(cond);
      active_high = true;
      return true;
    }
    if (k == "tuple" || k == "paren_group") {
      return ts_node_named_child_count(cond) == 1 && reset_condition(ts_node_named_child(cond, 0), port, active_high);
    }
    if (k == "unary_expression") {
      const auto op = kind_of(field(cond, "operator"));
      if ((op != "op_log_not" && op != "op_bit_not") || !reset_condition(field(cond, "argument"), port, active_high)) {
        return false;
      }
      active_high = !active_high;
      return true;
    }
    if (k == "expression_item" && ts_node_named_child_count(cond) == 3) {
      const auto lhs = ts_node_named_child(cond, 0), op = ts_node_named_child(cond, 1), rhs = ts_node_named_child(cond, 2);
      if (kind_of(op) != "binary_compare_op" || (text(op) != "==" && text(op) != "!=")) {
        return false;
      }
      const bool eq   = text(op) == "==";
      auto       ident = lhs, konst = rhs;
      if (kind_of(lhs) == "constant") {
        ident = rhs;
        konst = lhs;
      }
      if (kind_of(ident) != "identifier" || kind_of(konst) != "constant") {
        return false;
      }
      const auto v = text(konst);
      if (v != "0" && v != "1" && v != "true" && v != "false") {
        return false;
      }
      port        = text(ident);
      active_high = eq == (v == "1" || v == "true");
      return true;
    }
    return false;
  }

  // A scope whose every statement is `reg = CONSTANT` (at least one).
  bool constant_reg_arm(TSNode scope, const std::map<std::string_view, RegInfo>& regs, ResetIf& out) const {
    bool ok = true;
    children(scope, [&](TSNode st, const char*) {
      const auto k = kind_of(st);
      if (!ts_node_is_named(st) || k == "comment") {
        return;
      }
      Path dst;
      if (!ok || !plain_assignment(st, dst) || dst.size() != 1 || kind_of(field(st, "rvalue")) != "constant") {
        ok = false;
        return;
      }
      const auto it = regs.find(dst.front());
      if (it == regs.end() || !it->second.eligible || it->second.has_reset) {
        ok = false;
        return;
      }
      out.regs.push_back(it->first);
      out.assigns.push_back(st);
      if (out.value.empty()) {
        out.value = text(field(st, "rvalue"));
      }
    });
    return ok && !out.regs.empty();
  }

  static std::string_view root_name(TSNode n, const std::string_view src) {
    while (!ts_node_is_null(n) && std::string_view(ts_node_type(n)) != "identifier") {
      TSNode next{};
      for (uint32_t i = 0; i < ts_node_named_child_count(n) && ts_node_is_null(next); ++i) {
        auto c = ts_node_named_child(n, i);
        if (std::string_view(ts_node_type(c)) != "comment") {
          next = c;
        }
      }
      n = next;
    }
    return ts_node_is_null(n) ? std::string_view{} : src.substr(ts_node_start_byte(n), ts_node_end_byte(n) - ts_node_start_byte(n));
  }

  bool writes_any(TSNode n, const std::vector<std::string_view>& regs) const {
    bool hit = false;
    descend(n, [&](TSNode c) {
      if (hit || kind_of(c) == "lambda") {
        return false;
      }
      if (kind_of(c) == "assignment") {
        const auto name = root_name(field(c, "lvalue"), source);
        hit             = std::find(regs.begin(), regs.end(), name) != regs.end();
      }
      return !hit;
    });
    return hit;
  }

  void analyze_reset_if(const std::vector<TSNode>& stmts, size_t idx, const std::map<std::string_view, RegInfo>& regs,
                        std::map<std::string_view, Port>& ports, std::vector<ResetIf>& found) const {
    const auto node = stmts[idx];
    if (kind_of(node) != "if_expression" || ts_node_has_error(node)) {
      return;
    }
    TSNode cond{}, first{}, other{};
    size_t conditions = 0;
    bool   valid      = true;
    children(node, [&](TSNode c, const char* name) {
      const std::string_view f = name ? name : "";
      if (kind_of(c) == "unique" || f == "init") {
        valid = false;
      } else if (f == "condition" && conditions++ == 0) {
        cond = c;
      } else if (f == "code" && ts_node_is_null(first)) {
        first = c;
      } else if (f == "else") {
        other = c;
      }
    });
    std::string_view port;
    bool             high = true;
    if (!valid || ts_node_is_null(first) || !reset_condition(cond, port, high)) {
      return;
    }
    const auto pit = ports.find(port);
    if (pit == ports.end()) {
      return;
    }
    ResetIf r{node, port, {}, high, {}, {}, 0};
    ResetIf alt = r;
    const bool first_const = constant_reg_arm(first, regs, r);
    const bool other_const = !ts_node_is_null(other) && constant_reg_arm(other, regs, alt);
    if (first_const && !other_const) {
      r.removable = tokens(cond) + tokens(first);
    } else if (other_const && !first_const && conditions == 1) {
      r             = alt;  // the clear is the `else` arm: it runs when the condition is false
      r.active_high = !high;
      r.removable   = tokens(cond) + tokens(other);
    } else {
      return;  // both arms constant (a data mux of constants) or neither
    }
    for (size_t i = idx + 1; i < stmts.size(); ++i) {
      if (writes_any(stmts[i], r.regs)) {
        return;  // a later write wins over the clear, so it is not a priority reset
      }
    }
    ++pit->second.hardcoded;
    found.push_back(std::move(r));
  }

  void resets(TSNode lambda) {
    TSNode sig{};
    children(lambda, [&](TSNode c, const char*) {
      if (kind_of(c) == "function_definition_decl") {
        sig = c;
      }
    });
    const auto code = field(lambda, "code");
    if (ts_node_is_null(sig) || ts_node_is_null(code) || ts_node_is_null(field(sig, "input"))) {
      return;
    }
    std::map<std::string_view, Port>    ports;
    std::map<std::string_view, RegInfo> regs;
    children(field(sig, "input"), [&](TSNode a, const char*) {
      if (kind_of(a) != "typed_identifier") {
        return;
      }
      const auto id = field(a, "identifier"), ty = field(field(a, "type"), "type");
      if (ts_node_is_null(id) || ts_node_is_null(ty)) {
        return;
      }
      const auto tk = kind_of(ty);
      if (tk == "reset_type") {
        ports.try_emplace(text(id), Port{a, "Reset"});
      } else if (tk == "bool_type") {
        ports.try_emplace(text(id), Port{a, "Bool"});
      } else if (tk == "uint_type" && text(ty) == "U1") {
        ports.try_emplace(text(id), Port{a, "U1"});
      }
    });
    if (ports.empty()) {
      return;
    }
    if (const auto output = field(sig, "output"); !ts_node_is_null(output)) {
      bool             reg_next = false;
      std::string_view last_reg;
      children(output, [&](TSNode a, const char* name) {
        const std::string_view f = name ? name : "";
        if (f == "mod") {
          reg_next = text(a) == "reg";
        } else if (f == "definition") {
          if (!last_reg.empty() && !(kind_of(a) == "identifier" && text(a) == "nil")) {
            regs.at(last_reg).has_reset = true;
          }
        } else if (kind_of(a) == "typed_identifier") {
          last_reg = {};
          if (reg_next && !ts_node_is_null(field(a, "identifier"))) {
            last_reg = text(field(a, "identifier"));
            regs.try_emplace(last_reg);
          }
          reg_next = false;
        }
      });
    }
    std::vector<TSNode> scopes;
    descend(code, [&](TSNode n) {
      const auto k = kind_of(n);
      if (k == "lambda") {
        return false;
      }
      if (k == "identifier") {
        if (const auto it = ports.find(text(n)); it != ports.end()) {
          ++it->second.total;
        }
        return false;
      }
      if (k == "scope_statement") {
        scopes.push_back(n);
      } else if (k == "assignment") {
        const auto decl = field(n, "decl");
        const auto lv   = field(n, "lvalue");
        if (!ts_node_is_null(decl) && kind_of(field(decl, "storage")) == "reg_decl" && !ts_node_is_null(lv)) {
          RegInfo info;
          auto    id = lv;
          if (kind_of(lv) == "typed_identifier") {
            id             = field(lv, "identifier");
            const auto cast = field(lv, "type");
            if (!ts_node_is_null(cast)) {
              // The `attribute` field also labels the ':' token; find the bracket list itself.
              children(cast, [&](TSNode c, const char*) {
                if (kind_of(c) == "attribute_sq") {
                  info.has_attributes = true;
                  const auto list = text(c);
                  if (list.find("latch") != std::string_view::npos) {
                    info.eligible = false;
                  }
                  if (list.find("reset_pin") != std::string_view::npos && list.find("reset_pin=false") == std::string_view::npos) {
                    info.has_reset = true;
                  }
                }
              });
              if (const auto t = field(cast, "type"); !ts_node_is_null(t)) {
                info.type = std::string(text(t));
                if (kind_of(t) == "array_type") {
                  info.eligible = false;
                }
              }
            }
          }
          const auto init = field(n, "rvalue");
          if (!ts_node_is_null(init) && !(kind_of(init) == "identifier" && text(init) == "nil")) {
            info.has_reset = true;
          }
          if (!ts_node_is_null(id) && kind_of(id) == "identifier") {
            regs.try_emplace(text(id), std::move(info));
          }
        }
      } else if (k == "attribute_assignment") {
        const auto lv = field(n, "lvalue"), rv = field(n, "rvalue");
        if (!ts_node_is_null(lv) && !ts_node_is_null(rv) && text(lv) == "reset_pin" && kind_of(rv) == "identifier") {
          if (const auto it = ports.find(text(rv)); it != ports.end()) {
            ++it->second.structural;
          }
        }
      }
      return true;
    });
    std::vector<ResetIf> found;
    for (const auto scope : scopes) {
      std::vector<TSNode> stmts;
      children(scope, [&](TSNode c, const char*) {
        if (ts_node_is_named(c) && kind_of(c) != "comment") {
          stmts.push_back(c);
        }
      });
      for (size_t i = 0; i < stmts.size(); ++i) {
        analyze_reset_if(stmts, i, regs, ports, found);
      }
    }
    const auto pure = [&](std::string_view name) {
      const auto& p = ports.at(name);
      return p.total > 0 && p.total == p.hardcoded + p.structural;
    };
    for (auto& r : found) {
      if (!pure(r.port)) {
        continue;
      }
      std::string names;
      for (auto reg : r.regs) {
        names += (names.empty() ? "" : ", ");
        names += reg;
      }
      const std::string polarity = r.active_high ? "active-high" : "active-low";
      const std::string attr     = std::format("reset_pin={}{}", r.port, r.active_high ? "" : ", negreset=true");
      Finding f{};
      f.rule    = Rule::HardcodedReset;
      f.range   = range(r.node, r.node);
      f.message = std::format("'{}' is cleared to a constant by an 'if' on '{}', which is used only as a reset", names, r.port);
      f.hint    = std::format(
          "make the reset structural: declare the register with ':[{}] = {}' and delete the clearing arm, keeping the "
          "other arm as the normal next-state logic; reset_pin has priority over later writes and a sync reset is the default "
          "(async=true for an asynchronous one)",
          attr,
          r.value);
      const auto reg = regs.at(r.regs.front());
      if (r.regs.size() == 1 && !reg.type.empty() && !reg.has_attributes) {
        f.hint += std::format("; e.g. 'reg {}:{}:[{}] = {}'", names, reg.type, attr, r.value);
      }
      f.attributes = {
          {     "reset",                  std::string(r.port)},
          {"registers",                                 names},
          { "polarity",                              polarity},
          {     "value",                  std::string(r.value)},
          {"port_type",            ports.at(r.port).type}
      };
      f.score = r.removable;
      for (auto a : r.assigns) {
        related(f, a, "register cleared to a constant");
      }
      candidates.push_back(std::move(f));
    }
    for (const auto& [name, port] : ports) {
      if (port.type == "Reset" || !pure(name) || port.hardcoded + port.structural == 0) {
        continue;
      }
      Finding f{};
      f.rule    = Rule::ResetPortType;
      f.range   = range(port.node, port.node);
      f.message = std::format("'{}' only acts as a reset but is declared '{}'", name, port.type);
      f.hint    = std::format(
          "declare it as '{}:Reset': clocks and resets bind by type, not by name; polarity stays per register (negreset=true)",
          name);
      f.attributes = {
          {         "port",        std::string(name)},
          {"declared_type",                port.type},
          {   "reset_uses", std::to_string(port.hardcoded + port.structural)}
      };
      f.score = 1 + port.hardcoded + port.structural;
      candidates.push_back(std::move(f));
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
    if (!ts_node_has_error(node)) {
      bundle_arguments(node);
      if (kind == "lambda") {
        resets(node);
      }
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
      if (!ts_node_is_named(c) && !ts_node_is_missing(c)) {
        return;
      }
      if (std::string_view(ts_node_type(c)) == "comment") {
        directive(c, node);
      } else {
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
    if (!allows.empty()) {
      std::erase_if(candidates, [&](const Finding& f) {
        const bool drop = allowed(f);
        suppressed += drop;
        return drop;
      });
    }
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
    report.suppressed   = suppressed;
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
    case Rule::HardcodedReset               : return "hardcoded-reset";
    case Rule::ResetPortType                : return "reset-port-type";
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
