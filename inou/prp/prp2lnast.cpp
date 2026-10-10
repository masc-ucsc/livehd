//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp2lnast.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "battr.hpp"  // THE attribute vocabulary (//upass/core:battr_hdr, header-only)
#include "diag.hpp"
#include "hlop/memory_init.hpp"
#include "pass.hpp"
#include "perf_tracing.hpp"  // TRACE_EVENT — no-op unless built with --define profiling=1
#include "prp_builtins.hpp"
#include "prpparse/lexer.hpp"
#include "prpparse/parser.hpp"
#include "prpparse/prp_diag.hpp"
#include "prpparse/source_buffer.hpp"
#include "prpparse/token.hpp"
#include "range_bits.hpp"  // kMaxIntTypeWidth, std_clog2 (//upass/core:range_bits_hdr, header-only)
#include "source_path.hpp"
#include "str_tools.hpp"
#include "synth_policy.hpp"

static constexpr std::string_view call_ref_arg_marker           = "__ref_arg";
// Marks the receiver actual of a UFCS method call `obj.method(...)`
// so the runner can reject UFCS onto a self-less callee (the direct call form
// `method(obj, ...)` lowers without it). Positional, like __ref_arg.
static constexpr std::string_view call_ufcs_arg_marker          = "__ufcs_arg";
// Marks an explicit call-site generic binding (`f<int,string>(…)`): one
// `store(__generic_arg, type_ref)` per type argument, in declaration order,
// emitted BEFORE the normal actuals. The type_ref is either a named-type ref
// or a `declare(tmp, prim_type, 'type')` tmp the runner resolves through the
// same decl-facts ladder as a declared variable.
static constexpr std::string_view call_generic_arg_marker       = "__generic_arg";
// Marks a call-argument spread (`f(..., ...rest)`): the runner expands the
// referenced bundle's fields into named (`b=…`) / positional actuals at call
// resolution, so `foo(a=1, ...rest)` binds rest's fields to the matching
// params. The value child is the spread bundle's ref.
static constexpr std::string_view call_spread_arg_marker        = "__spread_arg";
// Marks a call-site instance name (`alu::[name=pipeB_ex_mem](…)`): the value
// child is a const string with the user-chosen instance name. Consumed (never
// bound to a callee port) by the runner inliner — which uses it as the
// hierarchical prefix for the inlined regs/mems — and by tolg, which uses it
// as the Sub instance name when the callee is a non-inlined pipe/mod.
static constexpr std::string_view call_inst_name_marker         = "__inst_name";
// Child of a kept (non-streamed) func_def's kind const: the lambda was declared
// `::[timecheck=false]`. upass/func_extract stamps the extracted lambda (and
// only it) as timing-check free; keep the spelling in sync there.
static constexpr std::string_view lambda_timecheck_false_marker = "__timecheck_false";

namespace {
std::string slurp_file(std::string_view filename) {
  std::string   fname(filename);
  auto          ss = std::ostringstream{};
  std::ifstream file(fname);
  ss << file.rdbuf();
  return ss.str();
}
}  // namespace

// Canonicalize a Pyrope escaped identifier (`` `name` `` → `name` when the inner
// text is a plain alnum/underscore word). Defined below; forward-declared here so
// the early lvalue-path lowering (member/dot field names) can canonicalize field
// keys identically to the read path. See the definition for the full contract.
[[nodiscard]] static std::string_view canonical_escaped_ident(std::string_view name);

// File-backed entry: read `filename` from disk, then delegate to the buffer ctor.
// Largest bracket-nesting depth we let reach the recursive-descent parser. The
// prpparse expression grammar recurses several stack frames per nesting level
// with no depth limit, so a pathologically nested expression (~12k deep)
// overflows the stack and SIGSEGVs the process. We reject deeper input up front
// with a clean syntax diagnostic. The cap is far above any real source and well
// below the overflow threshold.
static constexpr int kMaxParseNesting = 4000;

// `s[i]`/`s[i+1]` is the `/*` opening a Pyrope block comment. Block comments
// NEST (`/* a /* b */ c */` is one comment, matching the prpparse lexer and the
// tree-sitter scanner), so every raw-text comment skipper in this file must use
// this instead of stopping at the first `*/`. Returns the offset just past the
// matching `*/`, or s.size() for an unterminated comment.
static size_t skip_nested_block_comment(std::string_view s, size_t i) {
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

// Approximate the peak recursive-descent parser depth `src` would induce, so we
// can reject stack-overflowing input before it reaches the parser. Two unbounded
// recursion drivers exist: bracket nesting (`(`/`[`/`{` -> parse_paren etc.) and
// a run of leading unary operators (`!`/`~`/`-`/`+` -> parse_unary). Both leave a
// frame on the stack, so the live depth ~= open brackets + pending unary run.
// We track that sum and return its peak (string-literal and comment bytes are
// skipped). *deepest_byte gets the offset where the peak occurred.
static int max_parser_depth(std::string_view src, size_t& deepest_byte) {
  int    bracket = 0, unary_run = 0, peak = 0;
  int    comment_depth                                                         = 0;  // Pyrope block comments nest
  size_t peak_byte                                                             = 0;
  // String lexing matches the prpparse lexer: '…' has no escapes, "…" has
  // escapes and `{…}` holes that hold CODE (comments, nested strings, brackets),
  // and a backtick name is one token. A `//` or `"` inside any of them must not
  // switch the scan into a comment or string (that hid a deep expression from
  // this guard). `holes` keeps, per open hole, its inner `{` depth.
  enum { code, line_comment, block_comment, dq_string, sq_string, bt_name } st = code;
  std::vector<int> holes;
  for (size_t i = 0; i < src.size(); ++i) {
    const char c = src[i];
    switch (st) {
      case line_comment:
        if (c == '\n') {
          st = code;
        }
        break;
      case block_comment:
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
          ++comment_depth;
          ++i;
        } else if (c == '*' && i + 1 < src.size() && src[i + 1] == '/') {
          if (--comment_depth == 0) {
            st = code;
          }
          ++i;
        }
        break;
      case dq_string:
        if (c == '\\') {
          if (i + 2 < src.size() && src[i + 1] == 'u' && src[i + 2] == '{') {
            size_t k = i + 3;  // `\u{N}` carries its own braces (no hole)
            while (k < src.size() && src[k] != '}' && src[k] != '"') {
              ++k;
            }
            i = (k < src.size() && src[k] == '}') ? k : k - 1;
          } else {
            ++i;  // skip the escaped byte
          }
        } else if (c == '"') {
          st = code;
        } else if (c == '{' && i + 1 < src.size() && src[i + 1] == '{') {
          ++i;  // `{{` is a literal brace, not a hole (istring_pieces agrees)
        } else if (c == '{') {
          holes.push_back(0);  // a hole: code until its matching `}`
          ++bracket;           // the hole is sub-parsed as an expression
          st = code;
        }
        break;
      case sq_string:
        if (c == '\'' || c == '\n') {
          st = code;
        }
        break;
      case bt_name:
        if (c == '\\') {
          ++i;
        } else if (c == '`' || c == '\n') {
          st = code;
        }
        break;
      case code:
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
          st = line_comment;
          ++i;
        } else if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
          st            = block_comment;
          comment_depth = 1;
          ++i;
        } else if (c == '"') {
          st = dq_string;
        } else if (c == '\'') {
          st = sq_string;
        } else if (c == '`') {
          st = bt_name;
        } else if (c == '}' && !holes.empty() && holes.back() == 0) {
          holes.pop_back();  // the hole's own `}`: back in its string
          if (bracket > 0) {
            --bracket;
          }
          st = dq_string;
        } else if (c == '(' || c == '[' || c == '{') {
          if (c == '{' && !holes.empty()) {
            ++holes.back();
          }
          ++bracket;
        } else if (c == ')' || c == ']' || c == '}') {
          if (c == '}' && !holes.empty()) {
            --holes.back();
          }
          if (bracket > 0) {
            --bracket;
          }
        } else if (c == '!' || c == '~' || c == '-' || c == '+') {
          ++unary_run;  // a chain of prefix operators recurses one frame each
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
          unary_run = 0;  // any operand/atom consumes the pending prefix run
        }
        if (bracket + unary_run > peak) {
          peak      = bracket + unary_run;
          peak_byte = i;
        }
        break;
    }
  }
  deepest_byte = peak_byte;
  return peak;
}

Prp2lnast::Prp2lnast(std::string_view filename, std::string_view module_name)
    : Prp2lnast(filename, module_name, slurp_file(filename)) {}

// Buffer-backed entry (LSP / unsaved buffers): analyze `source` verbatim.
Prp2lnast::Prp2lnast(std::string_view filename, std::string_view module_name, std::string_view source) {
  // One slice per parsed file (profiling builds) — parse cost is per-file, so
  // this is the "which file is slow" view of the trace.
  TRACE_EVENT("pyrope", "prp2lnast", "file", std::string(filename));
  lnast       = std::make_shared<Lnast>(module_name);
  root_lnast_ = lnast;

  lnast->set_root(Lnast_ntype::create_top());

  // Builder co-owns lnast so its tmp counter and (future) cursor track the
  // same tree Prp2lnast is mutating directly.
  builder.lnast = lnast;

  src_filename = std::string(filename);
  src_relpath  = livehd::srcloc::workspace_relative(filename);
  prp_file     = std::string(source);

  // Whole-file ingestion: the locator derives the content hash + line-offset
  // table (1-based line:col at diagnostic-emission time) and keeps the bytes
  // for sourcesContent egress, shared by pointer across every carry.
  if (!src_relpath.empty()) {
    lnast->source_locator().set_file_content(src_relpath, std::string(prp_file));
  }

  // Excerpt provider while the parse runs: a staged error thrown below emits
  // (via parser_error_int -> Sink::flush) before this frame unwinds, so the
  // scope is still active when the human line renders.
  livehd::diag::Locator_scope diag_scope(&lnast->source_locator());

  // Guard the recursive-descent parser against stack-overflow on pathologically
  // nested input (the parser has no depth limit). Reject it with a clean syntax
  // diagnostic before parsing, pointing at the deepest opener.
  {
    size_t    deep_byte = 0;
    const int max_nest  = max_parser_depth(prp_file, deep_byte);
    if (max_nest > kMaxParseNesting) {
      uint32_t line = 1, col = 1;
      for (size_t i = 0; i < deep_byte && i < prp_file.size(); ++i) {
        if (prp_file[i] == '\n') {
          ++line;
          col = 1;
        } else {
          ++col;
        }
      }
      report_error_at(static_cast<uint32_t>(deep_byte),
                      static_cast<uint32_t>(deep_byte + 1),
                      line,
                      col,
                      line,
                      col + 1,
                      "nesting-too-deep",
                      "syntax",
                      std::format("expression nesting too deep ({} levels; max {})", max_nest, kMaxParseNesting),
                      "simplify deeply nested parentheses/brackets");
    }
  }

  // prpparse fail-fast: the first syntax error throws prpparse::Parse_error
  // carrying a rich Diag (shape-compatible with livehd::diag). Map it into the
  // LiveHD sink + parser_error so it surfaces exactly like a tree-sitter-era
  // syntax diagnostic. A throwing constructor skips the destructor, so the
  // parser/buffer unique_ptrs are reset before rethrowing.
  try {
    // `from_file`-style buffer, but the bytes are already in `prp_file` (the ctor
    // read the file / took the LSP buffer). The locator path must be relative.
    prp_buf    = std::make_unique<prpparse::Source_buffer>(src_relpath.empty() ? src_filename : src_relpath, prp_file);
    prp_parser = std::make_unique<prpparse::Parser>(*prp_buf);

    // 2f-stream: process_description pulls one top-level construct at a time from
    // prp_parser->parse_next() and lowers it, recycling the parser's CST arena
    // before the next — so no whole-file parse tree is built on the LiveHD path
    // (resident parse-tree memory stays bounded to one construct). The CLI /
    // --sexp / differential-oracle paths still use the whole-file parse_ast() /
    // parse() builders. check_parse_errors() is moot: prpparse fails fast (no
    // MISSING/ERROR nodes), so a returning parse is well-formed.

    process_description();
  } catch (const prpparse::Parse_error& pe) {
    report_prpparse_error(pe.diag);  // stages the diag + throws Eprp::parser_error
  } catch (...) {
    prp_parser.reset();
    prp_buf.reset();
    throw;
  }
}

Prp2lnast::~Prp2lnast() = default;

std::string_view Prp2lnast::get_text(const TSNode& node) const {
  auto start  = ts_node_start_byte(node);
  auto end    = ts_node_end_byte(node);
  auto length = end - start;

  I(end <= prp_file.size());
  // `foo` and foo are the SAME identifier whenever the escaped text is plain
  // identifier characters (`foo[bar]` stays a distinct, escaped name). Every
  // name the front end reads from source -- builtin callee (`assume`), type-call
  // key (`bits`), attribute, named argument, generic, tuple field -- goes
  // through here, so canonicalizing at the one text accessor makes every raw
  // spelling comparison and lookup honor the rule. Only a node whose WHOLE
  // text is one escaped plain identifier changes (the inner text may not hold
  // a backtick, space or operator), so a larger node's text is untouched.
  const auto txt = str_tools::canonical_escaped_ident(std::string_view(prp_file).substr(start, length));
  // A backtick name reads the STRING escapes (docs 02-basics "Identifiers";
  // `` `d\\e` `` is the identifier `d\e`, a Verilog escaped id). Decode a whole
  // single backtick token once, keeping the quotes, so the LNAST name is the
  // identifier's value -- the spelling every consumer (tolg, cgen, the Verilog
  // reader) already uses for it.
  if (txt.size() >= 3 && txt.front() == '`' && txt.back() == '`' && txt.find('\\') != std::string_view::npos) {
    if (auto dec = str_tools::decode_backtick_ident(txt)) {
      decoded_names_.push_back(std::move(*dec));
      // The decoded value can itself be a plain identifier (`` `c\u{41}` ``
      // decodes to `cA`): canonicalize AFTER decoding so it is the same name
      // as the bare spelling.
      return str_tools::canonical_escaped_ident(decoded_names_.back());
    }
  }
  return txt;
}

std::string_view Prp2lnast::text_between(uint32_t start, uint32_t end) const {
  if (start > end) {
    return {};
  }
  I(end <= prp_file.size());
  return std::string_view(prp_file).substr(start, end - start);
}

livehd::diag::Span Prp2lnast::span_of_node(const TSNode& node) const {
  livehd::diag::Span span;
  span.file       = src_filename;
  span.start_byte = ts_node_start_byte(node);
  span.end_byte   = ts_node_end_byte(node);
  auto sp         = ts_node_start_point(node);
  auto ep         = ts_node_end_point(node);
  span.start_line = sp.row + 1;
  span.start_col  = sp.column + 1;
  span.end_line   = ep.row + 1;
  span.end_col    = ep.column + 1;
  return span;
}

void Prp2lnast::stage_error(livehd::diag::Span span, std::string_view code, std::string_view category, std::string message,
                            std::string_view hint) const {
  auto msg_copy = message;
  // Stage the rich record; the throw path (parser_error_int -> sink.flush) emits
  // it exactly once and prints the `livehd: error:` line.
  livehd::diag::sink().stage(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                      .code     = std::string(code),
                                                      .category = std::string(category),
                                                      .pass     = "inou.prp",
                                                      .message  = std::move(message),
                                                      .span     = std::move(span),
                                                      .hint     = std::string(hint)});
  throw Eprp::parser_error(Pass::eprp, msg_copy);
}

void Prp2lnast::report_error(const TSNode& node, std::string_view code, std::string_view category, std::string message,
                             std::string_view hint) const {
  stage_error(span_of_node(node), code, category, std::move(message), hint);
}

void Prp2lnast::report_error_at(uint32_t start_byte, uint32_t end_byte, uint32_t start_line, uint32_t start_col, uint32_t end_line,
                                uint32_t end_col, std::string_view code, std::string_view category, std::string message,
                                std::string_view hint) const {
  livehd::diag::Span span;
  span.file       = src_filename;
  span.start_byte = start_byte;
  span.end_byte   = end_byte;
  span.start_line = start_line;
  span.start_col  = start_col;
  span.end_line   = end_line;
  span.end_col    = end_col;
  stage_error(std::move(span), code, category, std::move(message), hint);
}

void Prp2lnast::report_warning(const TSNode& node, std::string_view code, std::string_view category, std::string message,
                               std::string_view hint) const {
  // A warning never aborts the parse — emit it straight to the sink (which
  // writes the configured JSONL/human channels) and continue lowering.
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::warning,
                                                     .code     = std::string(code),
                                                     .category = std::string(category),
                                                     .pass     = "inou.prp",
                                                     .message  = std::move(message),
                                                     .span     = span_of_node(node),
                                                     .hint     = std::string(hint)});
}

bool Prp2lnast::expr_has_side_effects(TSNode n) const {
  if (ts_node_is_null(n)) {
    return false;
  }
  std::string_view t(ts_node_type(n));
  // Node kinds that carry an observable effect on their own: a call may write
  // `ref` params / instantiate hardware, an assignment / enum-assignment
  // mutates state, an `expr::[attr=…]` set configures a port, a lambda is a
  // definition, and an embedded scope / if / match / loop runs statements whose
  // effects are not captured by the discarded top-level value.
  if (t == "function_call_expression" || t == "assignment" || t == "enum_assignment" || t == "attribute_set" || t == "lambda"
      || t == "if_expression" || t == "match_expression" || t == "scope_statement" || t == "while_statement" || t == "for_statement"
      || t == "loop_statement") {
    return true;
  }
  for (TSNode c : ts_node_named_children(n)) {
    if (expr_has_side_effects(c)) {
      return true;
    }
  }
  return false;
}

hhds::SourceId Prp2lnast::mint_src(const TSNode& node) const {
  if (ts_node_is_null(node) || src_relpath.empty()) {
    return hhds::SourceId_invalid;
  }
  const auto sp = ts_node_start_point(node);
  return lnast->source_locator().mint(src_relpath, ts_node_start_byte(node), ts_node_end_byte(node), sp.row + 1);
}

void Prp2lnast::attach_loc(const Lnast_nid& idx, const TSNode& node) {
  if (idx.is_invalid()) {
    return;
  }
  lnast->set_srcid(idx, mint_src(node));
}

namespace {
// ── Typed-declaration initializer kind check (shared by variables & params) ──
// A typed scalar declaration (`mut c:bool = …`, or a function parameter
// `b:bool = …`) constrains the initializer's kind: bool/int/string are distinct
// with NO implicit conversion. These mirror the upass `uPass_typecheck` kind
// model so prp2lnast can reject a literal initializer whose kind contradicts the
// declared type at parse time, identically for both declaration forms.
enum class Scalar_kind { unknown, integer, boolean, string, clock };

const char* scalar_kind_name(Scalar_kind k) {
  switch (k) {
    case Scalar_kind::integer: return "integer";
    case Scalar_kind::boolean: return "boolean";
    case Scalar_kind::string : return "string";
    case Scalar_kind::clock  : return "Clock";
    default                  : return "unknown";
  }
}

// Classify a literal initializer's scalar kind. Mirrors
// uPass_typecheck::seed_kind_from_const — keep the two in sync. A non-const, a
// `nil` copy, or the typeless single-bit unknown wildcard returns `unknown` (no
// static kind ⇒ no check; runtime/flow cases stay the upass pass's job).
Scalar_kind literal_scalar_kind(const Lnast_node& v) {
  if (!v.is_const()) {
    return Scalar_kind::unknown;
  }
  std::string_view t = v.get_name();
  if (t == "nil") {
    return Scalar_kind::unknown;  // `nil` is a legal initializer for any type
  }
  if (t == "true" || t == "false") {
    return Scalar_kind::boolean;
  }
  if (t == "0sb?" || t == "0ub?") {
    return Scalar_kind::unknown;  // single unknown bit is typeless (bool or int)
  }
  if (!t.empty() && t.front() == '"') {
    return Scalar_kind::string;  // double-quoted string literal
  }
  try {
    auto c = Dlop::from_pyrope(t);
    if (c && c->is_integer()) {
      return Scalar_kind::integer;  // numeric / char literal
    }
  } catch (...) {
  }
  return Scalar_kind::unknown;
}

// Declared scalar kind of a type node (`bool_type` / `string_type` /
// `uint_type` / `sint_type` / `clock_type` / `reset_type`). Composite/named/none
// types do not constrain the scalar kind here, so they read as `unknown` (check
// skipped). A `Reset` is Bool-like (docs 07-typesystem "Clock and Reset": the
// constant `false` means no reset); a `Clock` takes no literal at all.
Scalar_kind declared_scalar_kind(TSNode ty) {
  if (ts_node_is_null(ty)) {
    return Scalar_kind::unknown;
  }
  std::string_view t(ts_node_type(ty));
  if (t == "bool_type" || t == "reset_type") {
    return Scalar_kind::boolean;
  }
  if (t == "clock_type") {
    return Scalar_kind::clock;
  }
  if (t == "string_type") {
    return Scalar_kind::string;
  }
  if (t == "uint_type" || t == "sint_type") {
    return Scalar_kind::integer;
  }
  return Scalar_kind::unknown;
}
}  // namespace

void Prp2lnast::check_decl_init_kind(std::string_view name, const Lnast_node& value, TSNode inner_type,
                                     const TSNode& anchor) const {
  // Shared by the variable-declaration arm of process_lvalue_for_assign and the
  // function-parameter emit_arg_assign: a typed scalar declaration's literal
  // initializer must match the declared kind (bool/int/string are distinct, no
  // implicit conversion). Both `mut c:bool = 10` and `comb f(b:bool = 3)` reach
  // here, so the diagnostic is identical for variables and parameters.
  Scalar_kind dk = declared_scalar_kind(inner_type);
  if (dk == Scalar_kind::unknown) {
    return;  // named/composite/unsized-named type — kind not constrained here
  }
  Scalar_kind vk = literal_scalar_kind(value);
  if (dk == Scalar_kind::clock && vk != Scalar_kind::unknown) {
    report_error(anchor,
                 "clock-const-bind",
                 "type",
                 std::format("`{}` is a `Clock`: a Clock is never bound to a constant (`{}`)", name, value.get_name()),
                 "drive it from a `Clock` input; to hold registers use an enable");
  }
  if (vk == Scalar_kind::unknown || vk == dk) {
    return;  // no static initializer kind, or kinds already agree
  }
  report_error(anchor,
               "decl-init-type-mismatch",
               "type",
               std::format("cannot initialize `{}` (declared {}) with {} value `{}`",
                           name,
                           scalar_kind_name(dk),
                           scalar_kind_name(vk),
                           value.get_name()),
               (dk == Scalar_kind::string || vk == Scalar_kind::string)
                   ? std::string("no implicit conversion — the initializer must match the declared type")
                   : std::format("no implicit conversion — the initializer must match the declared type, or cast "
                                 "explicitly: {}",
                                 upass::kBoolIntCastHint));
}

void Prp2lnast::report_error(std::string_view code, std::string_view category, std::string message, std::string_view hint) const {
  stage_error(livehd::diag::Span{}, code, category, std::move(message), hint);  // location-less (span = null)
}

void Prp2lnast::report_prpparse_error(const prpparse::Diag& d) const {
  livehd::diag::Span span;
  if (d.span.valid) {
    span.file       = src_filename;
    span.start_byte = d.span.start_byte;
    span.end_byte   = d.span.end_byte;
    span.start_line = d.span.start_line;
    span.start_col  = d.span.start_col;
    span.end_line   = d.span.end_line;
    span.end_col    = d.span.end_col;
  }
  // prpparse's notes (e.g. "'(' opened here") become a hint when none was set.
  std::string hint = d.hint;
  if (hint.empty() && !d.notes.empty()) {
    hint = d.notes.front().message;
  }
  stage_error(std::move(span),
              d.code,
              d.category.empty() ? std::string_view("syntax") : std::string_view(d.category),
              d.message,
              hint);
}

void Prp2lnast::report_error(const Lnast_nid& nid, std::string_view code, std::string_view category, std::string message,
                             std::string_view hint) const {
  auto span = lnast->span_of(nid);
  if (span.is_null()) {
    report_error(code, category, std::move(message), hint);  // no attached span — fall back to location-less
  }
  stage_error(std::move(span), code, category, std::move(message), hint);
}

namespace {
// Compiler SSA temps are `%`-prefixed (parser-impossible). A user-written
// `___6` is therefore NOT a temp — it is an ordinary name the scope checks
// (check_writes_in_scope / check_undefined_reads) must validate like any other.
bool prp_name_is_tmp(std::string_view n) { return Lnast::is_tmp(n); }

// `_` (sole arg) and `_0`/`_1`/… (positional args) are the implicit
// placeholder-lambda parameters (`comb add(a,b){ _0 + _1 }`). They are never
// declared yet are valid reads, so they are not "undefined". (A regular
// leading-underscore name like `_val` has a non-digit tail and is NOT a
// placeholder — it must be declared like any other variable.)
bool prp_name_is_placeholder_arg(std::string_view n) {
  if (n == "_") {
    return true;
  }
  if (n.size() < 2 || n[0] != '_') {
    return false;
  }
  for (size_t i = 1; i < n.size(); ++i) {
    if (n[i] < '0' || n[i] > '9') {
      return false;
    }
  }
  return true;
}

// Collect store/declare TARGET ref names within a func_def signature (the
// func_def children other than the body `stmts`) — its params and outputs.
void prp_collect_sig_targets(const Lnast* ln, const Lnast_nid& node, absl::flat_hash_set<std::string>& out) {
  auto t = ln->get_type(node);
  if (Lnast_ntype::is_store(t) || Lnast_ntype::is_declare(t)) {
    auto v = ln->get_first_child(node);
    if (!v.is_invalid() && Lnast_ntype::is_ref(ln->get_type(v))) {
      out.insert(std::string(ln->get_name(v)));
      // A defaulted comb input also declares the body-prologue local that
      // holds its default value (emit_arg_assign's `__default` sentinel).
      auto d = ln->get_sibling_next(v);
      if (!d.is_invalid() && Lnast_ntype::is_const(ln->get_type(d)) && ln->get_name(d) == "__default") {
        out.insert(Lnast_io_entry::default_value_name(ln->get_name(v)));
      }
    }
    return;
  }
  for (auto c = ln->get_first_child(node); !c.is_invalid(); c = ln->get_sibling_next(c)) {
    if (Lnast_ntype::is_stmts(ln->get_type(c))) {
      continue;  // never descend into the body
    }
    prp_collect_sig_targets(ln, c, out);
  }
}

// The generic parameter names of a func_def (`comb f<T, N>(…)`): the direct
// `ref` children of its generics tuple (func_def child index 2 — see
// process_lambda: name, kind, generics, inputs, outputs, body). A generic
// binds a comptime entity (type / constant / lambda) that is visible
// throughout the body — exactly like a param for name-resolution purposes
// (todo 3g subtask A), even though it carries no runtime value. Empty when the
// lambda has no `<…>` clause.
void prp_collect_generic_names(const Lnast* ln, const Lnast_nid& func_def, absl::flat_hash_set<std::string>& out) {
  auto c = ln->get_first_child(func_def);  // name
  if (c.is_invalid()) {
    return;
  }
  c = ln->get_sibling_next(c);  // kind const
  if (c.is_invalid()) {
    return;
  }
  c = ln->get_sibling_next(c);  // generics tuple_add
  if (c.is_invalid() || !Lnast_ntype::is_tuple_add(ln->get_type(c))) {
    return;
  }
  for (auto g = ln->get_first_child(c); !g.is_invalid(); g = ln->get_sibling_next(g)) {
    if (Lnast_ntype::is_ref(ln->get_type(g))) {
      out.insert(std::string(ln->get_name(g)));
    }
  }
}

}  // namespace

void Prp2lnast::check_undeclared_writes() const {
  std::vector<absl::flat_hash_set<std::string>> scope_stack;
  for (auto c = lnast->get_first_child(lnast->get_root()); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (Lnast_ntype::is_stmts(lnast->get_type(c))) {
      check_writes_in_scope(c, scope_stack, /*barrier=*/0, streamed_scope_names_);
    }
  }
}

// A statement-level `store` to a non-tmp name with no `mut`/`const`/declare (or
// param/output) visible in scope is `a = 3` with no prior declaration — an error.
// A `mut`/`const`/`reg` declaration (or a `for` iterator) reusing a name from an
// ENCLOSING scope is variable shadowing. Both resolved against `scope_stack` (a
// scoped symbol table built in this single walk); see the header.
void Prp2lnast::check_writes_in_scope(const Lnast_nid& scope_stmts, std::vector<absl::flat_hash_set<std::string>>& scope_stack,
                                      size_t barrier, const absl::flat_hash_set<std::string>& seed_here,
                                      const absl::flat_hash_set<std::string>& readonly_here) const {
  scope_stack.emplace_back(seed_here.begin(), seed_here.end());
  readonly_frames_.resize(scope_stack.size());
  readonly_frames_.back() = readonly_here;
  const size_t lvl        = scope_stack.size() - 1;

  // Declared in a STRICTLY enclosing frame (>= barrier, < this scope) — shadowing.
  auto in_enclosing = [&](std::string_view name) {
    for (size_t i = lvl; i-- > barrier;) {
      if (scope_stack[i].contains(name)) {
        return true;
      }
    }
    return false;
  };
  // Declared in any live frame (this scope or enclosing, >= barrier) — for
  // assign-no-decl.
  auto is_declared = [&](std::string_view name) {
    for (size_t i = lvl + 1; i-- > barrier;) {
      if (scope_stack[i].contains(name)) {
        return true;
      }
    }
    return false;
  };

  for (auto c = lnast->get_first_child(scope_stmts); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    const auto ct = lnast->get_type(c);

    // A bare `stmts` block (e.g. the for-loop unroll wrapper) is a nested scope.
    if (Lnast_ntype::is_stmts(ct)) {
      check_writes_in_scope(c, scope_stack, barrier);
      continue;
    }

    // A `for` node declares its binding vars — value (child0) plus the optional
    // idx (child4) / key (child5) of `for (value, idx, key) in t`. The
    // runner-unroll form carries no attr_set decl, so register them here: they
    // are visible inside the body and may be written, and a binding that reuses
    // an enclosing name is variable shadowing (located at the `for`). Layout:
    // for(value, iterable, body, mode [, idx [, key]]).
    if (Lnast_ntype::is_for(ct)) {
      absl::flat_hash_set<std::string> binds;
      absl::flat_hash_set<std::string> index_binds;
      Lnast_nid                        body_stmts;
      int                              pos = 0;
      for (auto cc = lnast->get_first_child(c); !cc.is_invalid(); cc = lnast->get_sibling_next(cc), ++pos) {
        if (pos == 2) {
          body_stmts = cc;  // the loop body
        } else if ((pos == 0 || pos == 4 || pos == 5) && Lnast_ntype::is_ref(lnast->get_type(cc))) {
          std::string name(lnast->get_name(cc));
          if (name.empty() || prp_name_is_tmp(name)) {
            continue;
          }
          if (pos != 0) {
            index_binds.insert(name);  // the index/key is const; the value is a copy
          }
          // Shadows if the name is already declared in this-or-an-enclosing scope
          // — `mut c` then `for c …` at the same level both forbid reusing `c`.
          if (is_declared(name)) {
            report_error(c,
                         "variable-shadowing",
                         "name",
                         std::format("variable shadowing: '{}' is already declared in an enclosing scope", name),
                         "rename the inner/loop variable, or assign without a new `mut`/`const`/`reg` to reuse the outer one");
          }
          binds.insert(std::move(name));
        }
      }
      if (!body_stmts.is_invalid()) {
        check_writes_in_scope(body_stmts, scope_stack, barrier, binds, index_binds);
      }
      continue;
    }

    auto first_ref_name = [&](const Lnast_nid& node) -> std::string {
      auto c0 = lnast->get_first_child(node);
      if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0))) {
        return std::string(lnast->get_name(c0));
      }
      return {};
    };

    // The declaration mode (child2 of `declare`/`attr_set`-type): "mut" / "const"
    // / "reg" for a variable, vs "type" / enum / func kinds we do not shadow-check.
    auto third_child_name = [&](const Lnast_nid& node) -> std::string_view {
      auto a = lnast->get_first_child(node);
      if (a.is_invalid()) {
        return {};
      }
      auto b = lnast->get_sibling_next(a);
      if (b.is_invalid()) {
        return {};
      }
      auto d = lnast->get_sibling_next(b);
      return d.is_invalid() ? std::string_view{} : lnast->get_name(d);
    };
    auto is_var_mode = [](std::string_view m) { return m == "mut" || m == "const" || m == "reg"; };

    // No-shadowing rule (04-variables.md "Variable scope"): a `mut`/`const`/`reg`
    // declaration — including a `for` loop iterator — may NOT reuse a name that is
    // already visible in an enclosing scope. (Plain re-assignment `a = e` is fine;
    // it is a `store`, not a declaration. `type`/enum/func declarations are not
    // shadow-checked here.) Located via the span attached to the declaring node.
    auto check_shadowing = [&](const Lnast_nid& node, const std::string& name) {
      if (tick_loop_var_decls_.contains(node)) {
        return;  // synthesized tick loop var — `lhd sim` owns its collision errors
      }
      if (!name.empty() && is_var_mode(third_child_name(node)) && in_enclosing(name)) {
        report_error(node,
                     "variable-shadowing",
                     "name",
                     std::format("variable shadowing: '{}' is already declared in an enclosing scope", name),
                     "rename the inner/loop variable, or assign without a new `mut`/`const`/`reg` to reuse the outer one");
      }
    };

    if (Lnast_ntype::is_declare(ct)) {
      auto name = first_ref_name(c);
      if (!name.empty()) {
        check_shadowing(c, name);
        scope_stack[lvl].insert(name);
      }
    } else if (Lnast_ntype::is_attr_set(ct)) {
      // Legacy declaration form still emitted by some lowerings (e.g. the
      // for-loop iter var): attr_set(var, "type", <mode>) declares `var`.
      auto c0 = lnast->get_first_child(c);
      auto c1 = c0.is_invalid() ? c0 : lnast->get_sibling_next(c0);
      if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0)) && !c1.is_invalid() && lnast->get_name(c1) == "type") {
        auto name = std::string(lnast->get_name(c0));
        check_shadowing(c, name);
        scope_stack[lvl].insert(name);
      }
    } else if (Lnast_ntype::is_store(ct)) {
      auto name = first_ref_name(c);
      if (!name.empty() && !prp_name_is_tmp(name) && !is_declared(name)) {
        report_error(c,
                     "assign-no-decl",
                     "name",
                     std::format("assignment to undeclared variable '{}' (declare it with `mut`/`const` first)", name));
      } else if (!name.empty() && !prp_name_is_tmp(name)) {
        // The innermost frame declaring the name decides: a `for` index is the
        // const position (05b-statements.md); writing one used to be accepted
        // and changed every later use in that iteration. (A non-`ref` lambda
        // input is a by-value copy the body may write -- tests/comptime/ref_comb.)
        for (size_t i = lvl + 1; i-- > barrier;) {
          if (!scope_stack[i].contains(name)) {
            continue;
          }
          if (readonly_frames_[i].contains(name)) {
            report_error(c,
                         "assign-readonly",
                         "name",
                         std::format("assignment to '{}', a `for` index (the const position of the element)", name),
                         "copy it into a `mut` local first; the loop VALUE is a writable copy");
          }
          break;
        }
      }
    }

    const bool                       is_func_body = Lnast_ntype::is_func_def(ct);
    absl::flat_hash_set<std::string> sig;
    if (is_func_body) {
      prp_collect_sig_targets(lnast.get(), c, sig);
    }
    for (auto cc = lnast->get_first_child(c); !cc.is_invalid(); cc = lnast->get_sibling_next(cc)) {
      if (Lnast_ntype::is_stmts(lnast->get_type(cc))) {
        if (is_func_body) {
          // Params/outputs belong to the body's OWN scope: seed it with `sig`, and
          // make the body a FRESH namespace (barrier = its frame) — a lambda does
          // not see outer runtime vars. A NESTED re-declaration still shadows.
          check_writes_in_scope(cc, scope_stack, /*barrier=*/scope_stack.size(), sig);
        } else {
          // An if/match arm: a nested scope that still sees the enclosing frames.
          check_writes_in_scope(cc, scope_stack, barrier);
        }
      }
    }
  }
  scope_stack.pop_back();
}

// Collect the order-independent names: function names (`comb/pipe/mod
// NAME(...)` — func_def child0, used as a value in higher-order calls like
// `apply_each(add_1)`) and type/enum declarations (declare/attr_set with a
// non-var mode). These are comptime entities — forward references are fine.
void Prp2lnast::collect_hoisted_names(const Lnast& ln, const Lnast_nid& node, absl::flat_hash_set<std::string>& hoisted) {
  const auto t = ln.get_type(node);
  if (Lnast_ntype::is_func_def(t)) {
    auto c0 = ln.get_first_child(node);
    if (!c0.is_invalid() && Lnast_ntype::is_ref(ln.get_type(c0))) {
      hoisted.insert(std::string(ln.get_name(c0)));
    }
  } else if (Lnast_ntype::is_declare(t) || Lnast_ntype::is_attr_set(t)) {
    // declare(name, TYPE, mode) — `type Foo = …` / `enum Foo = …` carry mode
    // "type". attr_set(name, "type", mode) is the pre-rewrite decl form; var
    // modes (mut/const/reg/stage) are program-order scoped, anything else is
    // a comptime entity.
    auto c0 = ln.get_first_child(node);
    if (!c0.is_invalid() && Lnast_ntype::is_ref(ln.get_type(c0))) {
      auto c1 = ln.get_sibling_next(c0);
      auto c2 = c1.is_invalid() ? c1 : ln.get_sibling_next(c1);
      if (!c2.is_invalid() && (Lnast_ntype::is_declare(t) || ln.get_name(c1) == "type")) {
        auto mode = ln.get_name(c2);
        if (mode != "mut" && mode != "const" && mode != "reg" && mode != "stage") {
          hoisted.insert(std::string(ln.get_name(c0)));
        }
      }
    }
  }
  for (auto c = ln.get_first_child(node); !c.is_invalid(); c = ln.get_sibling_next(c)) {
    collect_hoisted_names(ln, c, hoisted);
  }
}

// Lexical visibility of one recorded read: scan the recorded frame up to the
// read position (a declaration is visible from its statement onward), then
// climb the enclosing frames. Crossing a func_def boundary also checks its
// signature (params/outputs); names visible at the lambda's definition point
// stay readable inside the body. Whether a nested lambda may read an enclosing
// binding at all (comptime only) is checked when the read is lowered, see
// capture_frames_ / check_capture_read. Crossing a `for` node makes its
// iterator visible (the runner-unroll form carries no attr_set decl).
namespace {
// Names a statement-level child declares (mirror of read_is_visible's
// stmt_declares lambda). Used to precompute read_scope_decls_.
void prp_collect_stmt_decls(const Lnast& ln, const Lnast_nid& c, absl::flat_hash_set<std::string>& out) {
  const auto ct     = ln.get_type(c);
  auto       c0     = ln.get_first_child(c);
  const bool c0_ref = !c0.is_invalid() && Lnast_ntype::is_ref(ln.get_type(c0));
  if (Lnast_ntype::is_declare(ct)) {
    if (c0_ref) {
      out.insert(std::string(ln.get_name(c0)));
    }
  } else if (Lnast_ntype::is_attr_set(ct)) {
    auto c1 = c0.is_invalid() ? c0 : ln.get_sibling_next(c0);
    if (c0_ref && !c1.is_invalid() && ln.get_name(c1) == "type") {
      out.insert(std::string(ln.get_name(c0)));
    }
  } else if (Lnast_ntype::is_if_like(ct)) {
    for (auto gc = ln.get_first_child(c); !gc.is_invalid(); gc = ln.get_sibling_next(gc)) {
      if (!Lnast_ntype::is_stmts(ln.get_type(gc))) {
        prp_collect_stmt_decls(ln, gc, out);
      }
    }
  }
}
}  // namespace

bool Prp2lnast::read_is_visible(const Read_site& rs) const {
  // A `for` node binds value (child0) plus the optional idx (child4) / key
  // (child5) of `for (value, idx, key) in t`. Any of them is visible in the body.
  // Layout: for(value, iterable, body, mode [, idx [, key]]).
  auto for_binds_name = [&](const Lnast_nid& node) -> bool {
    int pos = 0;
    for (auto c = lnast->get_first_child(node); !c.is_invalid(); c = lnast->get_sibling_next(c), ++pos) {
      if ((pos == 0 || pos == 4 || pos == 5) && Lnast_ntype::is_ref(lnast->get_type(c)) && (lnast->get_name(c) == rs.name)) {
        return true;
      }
    }
    return false;
  };
  // Per-frame declaration lookups now use the precomputed read_scope_decls_ /
  // read_child_index_ (see check_undefined_reads); the old per-read sibling scan
  // (stmt_declares over every child up to the boundary) is gone.

  Lnast_nid frame    = rs.scope;
  Lnast_nid boundary = rs.before;  // inclusive; invalid = frame was empty at read time
  if (frame.is_invalid()) {
    return streamed_scope_names_.contains(rs.name);  // a streamed lambda's signature (no stmts frame yet)
  }

  while (!frame.is_invalid()) {
    // O(1) frame resolution (was an O(siblings) scan per read): `rs.name` is
    // declared in this frame at or before the read iff its earliest declaring
    // child index is <= the boundary child's index. read_scope_decls_ /
    // read_child_index_ are precomputed in check_undefined_reads; this matches the
    // old scan's IR boundary, with a source-order check for declarations moved
    // ahead of an earlier read (notably an elif initializer ahead of if bodies).
    if (!boundary.is_invalid()) {
      const auto sd = read_scope_decls_.find(frame);
      if (sd != read_scope_decls_.end()) {
        const auto di = sd->second.find(rs.name);
        const auto bi = read_child_index_.find(boundary);
        if (di != sd->second.end() && bi != read_child_index_.end() && di->second.index <= bi->second) {
          // A captured binding may also appear in streamed_scope_names_. Do
          // not let that fallback make a known later declaration visible.
          return di->second.start_byte <= rs.start_byte;
        }
      }
    }
    // Climb to the enclosing frame: walk up to the statement-level node that
    // contains this frame; its parent is the next frame and it becomes the
    // new (inclusive) boundary. Binding constructs on the way contribute
    // their introduced names.
    Lnast_nid child = frame;
    Lnast_nid p     = lnast->get_parent(frame);
    while (!p.is_invalid() && !lnast->is_root(p) && !Lnast_ntype::is_stmts(lnast->get_type(p))) {
      const auto pt = lnast->get_type(p);
      if (Lnast_ntype::is_func_def(pt)) {
        absl::flat_hash_set<std::string> sig;
        prp_collect_sig_targets(lnast.get(), p, sig);
        // Generic names (`comb f<T>(…)`) are visible in the body too — as a
        // type (`mut tmp:T`), a value (`a + N`), or a callee (`T(a)`, `F(v)`);
        // the runner substitutes the bound entity per call site (todo 3g A).
        prp_collect_generic_names(lnast.get(), p, sig);
        if (sig.contains(rs.name)) {
          return true;
        }
      } else if (Lnast_ntype::is_for(pt) && for_binds_name(p)) {
        // for(value, iterable, body, mode [, idx [, key]]) — runner-unroll form:
        // the value/idx/key binds are implicitly declared by the loop itself.
        return true;
      }
      child = p;
      p     = lnast->get_parent(p);
    }
    if (p.is_invalid() || lnast->is_root(p)) {
      if (streamed_scope_names_.contains(rs.name)) {
        return true;
      }
      break;  // ran out of enclosing frames
    }
    frame    = p;
    boundary = child;
  }
  return false;
}

bool Prp2lnast::name_in_inflight_scope(std::string_view name) const {
  for (const auto& frame : inflight_name_scopes_) {
    for (const auto& s : frame) {
      if ((s == name)) {
        return true;
      }
    }
  }
  return false;
}

namespace {
std::string prp_undefined_read_message(std::string_view name) {
  return std::format("read of undefined variable '{}' (not visible here: declared later, out of scope, or never)", name);
}
constexpr std::string_view prp_undefined_read_hint
    = "declare it with `mut`/`const`/`reg` before use — a variable is visible from its declaration to the end of its scope";
}  // namespace

void Prp2lnast::check_undefined_reads() const {
  absl::flat_hash_set<std::string> hoisted;
  collect_hoisted_names(*lnast, lnast->get_root(), hoisted);

  // Precompute, per stmts scope, name -> earliest declaring child index (and each
  // child's index), so read_is_visible resolves a frame in O(1) instead of an
  // O(siblings) scan per read (see read_scope_decls_ / read_child_index_). O(N).
  read_scope_decls_.clear();
  read_child_index_.clear();
  {
    std::function<void(const Lnast_nid&)> build = [&](const Lnast_nid& n) {
      if (Lnast_ntype::is_stmts(lnast->get_type(n))) {
        auto& decls = read_scope_decls_[n];
        int   idx   = 0;
        for (auto c = lnast->get_first_child(n); !c.is_invalid(); c = lnast->get_sibling_next(c), ++idx) {
          read_child_index_[c] = idx;
          absl::flat_hash_set<std::string> names;
          prp_collect_stmt_decls(*lnast, c, names);
          for (const auto& nm : names) {
            auto it = decls.find(nm);
            if (it == decls.end()) {
              const auto span  = lnast->span_of(c);
              // Only compare positions within this source file. Captured and
              // synthesized nodes without a local span still use IR order.
              const auto start = span.file == src_relpath ? span.start_byte.value_or(0) : 0;
              decls.emplace(nm, Read_declaration{idx, static_cast<uint32_t>(start)});
            }
          }
        }
      }
      for (auto c = lnast->get_first_child(n); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
        build(c);
      }
    };
    build(lnast->get_root());
  }

  for (const auto& rs : read_sites_) {
    if (rs.name.empty() || rs.name == "self" || prp_name_is_tmp(rs.name) || prp_name_is_placeholder_arg(rs.name)
        || hoisted.contains(rs.name) || streamed_function_names_.contains(rs.name)) {
      continue;
    }
    if (read_is_visible(rs)) {
      continue;
    }
    // Old type spellings are ordinary identifiers. Resolve declared names
    // first, then help an unresolved use migrate to the capitalized type.
    if (const auto renamed = str_tools::renamed_type_spelling(rs.name);
        !renamed.empty() && (rs.is_call || rs.is_type || rs.generic != Generic_read::none)) {
      report_error_at(
          rs.start_byte,
          rs.end_byte,
          rs.start_line,
          rs.start_col,
          rs.end_line,
          rs.end_col,
          "renamed-type",
          "name",
          std::format("`{}` was renamed `{}`", rs.name, renamed),
          std::format("use `{}` for the built-in type or cast, or declare `{}` before using it as a name", renamed, rs.name));
    }
    // report_error throws — the first undefined read aborts the parse. The
    // pre-captured span (2f-stream: the originating TSNode is gone) locates it.
    if (rs.is_call) {
      if (prp_builtins::is_removed_int_keyword(rs.name)) {
        // `int(x)`/`uint(x)`/`integer(x)` were removed in favor of explicit
        // sign. Tailored guidance (mirrors the upass.tolg note) rather than a
        // generic "undefined function".
        report_error_at(rs.start_byte,
                        rs.end_byte,
                        rs.start_line,
                        rs.start_col,
                        rs.end_line,
                        rs.end_col,
                        "removed-int-cast",
                        "type",
                        std::format("the `{}(...)` cast was removed — use `Signed(x)`/`Unsigned(x)` to reinterpret a "
                                    "value's sign, or a sized cast `U<N>(x)`/`S<N>(x)`",
                                    rs.name),
                        "Pyrope spells sign intent explicitly: `Signed`/`Unsigned` (or `S<N>`/`U<N>`)");
      } else if (rs.name == "Clock" || rs.name == "Reset") {
        // A type word, but not a cast (docs 07-typesystem "Clock and Reset").
        report_error_at(rs.start_byte,
                        rs.end_byte,
                        rs.start_line,
                        rs.start_col,
                        rs.end_line,
                        rs.end_col,
                        "no-clock-reset-cast",
                        "type",
                        rs.name == "Clock" ? std::string("`Clock(...)` is not a cast: a Clock is never made from data "
                                                         "(there are no derived clocks)")
                                           : std::string("`Reset(...)` is not a cast: a Reset is Bool-like, a `Bool` "
                                                         "expression binds to it without a cast"),
                        rs.name == "Clock" ? "drive a clock pin from a `Clock` input, or gate one with "
                                             "`Clock(clock_pin=clk, enable=en)`"
                                           : "pass the `Bool` expression itself (`reset_pin=rst or soft_rst`)");
      } else {
        report_error_at(
            rs.start_byte,
            rs.end_byte,
            rs.start_line,
            rs.start_col,
            rs.end_line,
            rs.end_col,
            "undefined-call",
            "name",
            std::format("call to undefined function '{}' (no such `comb`/`mod`/`pipe`, variable, or built-in)", rs.name),
            "define it with `comb`/`mod`/`pipe`, import it, or check the spelling");
      }
    } else if (rs.is_type && prp_builtins::is_removed_int_keyword(rs.name)) {
      // `a:int` / `a:uint` / `a:integer`: removed types, not an unknown name.
      report_error_at(rs.start_byte,
                      rs.end_byte,
                      rs.start_line,
                      rs.start_col,
                      rs.end_line,
                      rs.end_col,
                      "removed-int-type",
                      "type",
                      std::format("the `{}` type was removed — use `{}` (unbounded) or a sized `{}`",
                                  rs.name,
                                  rs.name == "uint" ? "Unsigned" : "Signed",
                                  rs.name == "uint" ? "U<N>" : "S<N>"),
                      "Pyrope spells sign intent explicitly: `Signed`/`Unsigned` (or `S<N>`/`U<N>`)");
    } else if (rs.is_type) {
      report_error_at(rs.start_byte,
                      rs.end_byte,
                      rs.start_line,
                      rs.start_col,
                      rs.end_line,
                      rs.end_col,
                      "unknown-type",
                      "type",
                      std::format("`{}` is not a type (no such `type`/`enum` is declared or in scope)", rs.name),
                      std::format("declare it with `type {0} = …` / `enum {0} = …`, or use a built-in type "
                                  "like `U4`/`Bool`/`String`",
                                  rs.name));
    } else if (rs.generic != Generic_read::none) {
      report_error_at(rs.start_byte,
                      rs.end_byte,
                      rs.start_line,
                      rs.start_col,
                      rs.end_line,
                      rs.end_col,
                      "unknown-generic-name",
                      "name",
                      std::format("unknown type or lambda '{}' in a generic {} (it is not declared before this point)",
                                  rs.name,
                                  rs.generic == Generic_read::default_value ? "default" : "argument"),
                      std::format("declare `{}` before its use: a generic default or argument names a type, a lambda or a "
                                  "comptime const declared earlier in the file",
                                  rs.name));
    } else if (rs.name == prp_builtins::std_namespace) {
      report_error_at(rs.start_byte,
                      rs.end_byte,
                      rs.start_line,
                      rs.start_col,
                      rs.end_line,
                      rs.end_col,
                      "std-not-a-value",
                      "name",
                      "`std` is the built-in namespace, not a value",
                      "call one of its functions, e.g. `std.clog2(x)`");
    } else {
      report_error_at(rs.start_byte,
                      rs.end_byte,
                      rs.start_line,
                      rs.start_col,
                      rs.end_line,
                      rs.end_col,
                      "undefined-read",
                      "name",
                      prp_undefined_read_message(rs.name),
                      prp_undefined_read_hint);
    }
  }
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

namespace {
// 2c-wire — does `nid` store a (non-nil) value into wire `w`? A `wire w = nil`
// forward-declare (a 2-child store of const nil) is NOT a driver.
bool prp_wire_driver_store(const Lnast& ln, const Lnast_nid& nid, std::string_view w) {
  if (!Lnast_ntype::is_store(ln.get_type(nid))) {
    return false;
  }
  auto c0 = ln.get_first_child(nid);
  if (c0.is_invalid() || !Lnast_ntype::is_ref(ln.get_type(c0)) || ln.get_name(c0) != w) {
    return false;
  }
  auto c1 = ln.get_sibling_next(c0);
  if (c1.is_invalid()) {
    return false;
  }
  if (ln.get_sibling_next(c1).is_invalid() && Lnast_ntype::is_const(ln.get_type(c1)) && ln.get_name(c1) == "nil") {
    return false;  // `wire w = nil` forward-declare
  }
  return true;
}

// 2c-wire — is `nid` the write-back half of a bit-range PARTIAL write to `w`?
// `w#[lo..=hi] = rhs` lowers to a read-modify-write PAIR (the bit_selection arm
// of process_lvalue_for_assign): `set_mask %t, w, mask, rhs` immediately
// followed by `store(w, %t)`. `prev` is the statement before `nid` in the same
// scope, which is that set_mask when — and only when — this store closes such a
// pair.
//
// Such a write REFINES the net's single driver rather than adding one:
// `w#[0..=3] = a; w#[4..=7] = b` is ONE packed net assembled from disjoint
// fields, exactly Verilog's `assign w[3:0]=a; assign w[7:4]=b`, and upass.tolg
// accumulates the whole chain on one din shadow (lower_set_mask). Counting each
// as a separate driver made the packed-wire class — the one shape that
// manufactured a set_mask self-reference — unwritable in Pyrope.
// `mask_out`, when given, receives the LANE this write covers: the constant
// mask bit_selection_ranges baked for `#[lo..=hi]`, or "" for a runtime lane.
// Callers need it because "refines the same value" only holds while the lanes
// are DISJOINT — two writes to the same bits are a genuine double-drive, and
// upass.tolg chains them on one din accumulator so the second silently wins.
bool prp_wire_partial_store(const Lnast& ln, const Lnast_nid& nid, const Lnast_nid& prev, std::string_view w,
                            std::string* mask_out) {
  if (mask_out != nullptr) {
    mask_out->clear();
  }
  if (prev.is_invalid() || !Lnast_ntype::is_set_mask(ln.get_type(prev))) {
    return false;
  }
  auto c0 = ln.get_first_child(nid);  // store's target ref (already checked == w)
  auto c1 = c0.is_invalid() ? c0 : ln.get_sibling_next(c0);
  if (c1.is_invalid() || !ln.get_sibling_next(c1).is_invalid() || !Lnast_ntype::is_ref(ln.get_type(c1))) {
    return false;  // not a plain 2-child `w = <ref>` store
  }
  auto d0 = ln.get_first_child(prev);                        // set_mask dst (the %t temp)
  auto d1 = d0.is_invalid() ? d0 : ln.get_sibling_next(d0);  // set_mask base
  if (d0.is_invalid() || d1.is_invalid() || !Lnast_ntype::is_ref(ln.get_type(d1))) {
    return false;
  }
  if (ln.get_name(d0) != ln.get_name(c1) || ln.get_name(d1) != w) {
    return false;
  }
  if (mask_out != nullptr) {
    auto value = ln.get_sibling_next(d1);
    auto lo    = ln.get_sibling_next(value);
    auto hi    = ln.get_sibling_next(lo);
    if (!lo.is_invalid() && !hi.is_invalid() && Lnast_ntype::is_const(ln.get_type(lo)) && Lnast_ntype::is_const(ln.get_type(hi))) {
      auto l = Dlop::from_pyrope(ln.get_name(lo));
      auto h = Dlop::from_pyrope(ln.get_name(hi));
      if (l->is_just_i64() && h->is_just_i64() && l->to_just_i64() >= 0 && h->to_just_i64() > l->to_just_i64()) {
        *mask_out = Dlop::get_mask_value(h->to_just_i64() - 1, l->to_just_i64())->to_pyrope();
      }
    }
  }
  return true;
}

// Bit LANES a wire's partial writes cover, so the multiple-driver check can
// tell a net ASSEMBLED from disjoint fields from a double-drive of the SAME
// bits. `dyn` holds wires with a runtime lane, where disjointness cannot be
// proven here.
struct Wire_lanes {
  absl::flat_hash_map<std::string, std::string> mask;  // pyrope text: OR of the constant lanes seen
  absl::flat_hash_set<std::string>              dyn;
  absl::flat_hash_set<std::string>              overlap;
};

// Fold one write's lane into `w`'s coverage. `detect` false unions without
// reporting — the arms of ONE if/match are mutually exclusive, so lanes they
// share are not a conflict; the union is then folded into the enclosing
// straight-line scope with detection on.
void wire_lane_add(Wire_lanes& lanes, std::string_view w, std::string_view m, bool detect) {
  const std::string key{w};
  const bool        seen = lanes.dyn.contains(key) || lanes.mask.contains(key);
  if (m.empty()) {
    if (detect && seen) {
      lanes.overlap.insert(key);  // a runtime lane over an already-driven wire
    }
    lanes.dyn.insert(key);
    return;
  }
  auto mv = Dlop::from_pyrope(std::string{m});
  if (mv->is_invalid() || !mv->is_integer()) {
    if (detect && seen) {
      lanes.overlap.insert(key);
    }
    lanes.dyn.insert(key);
    return;
  }
  if (detect && lanes.dyn.contains(key)) {
    lanes.overlap.insert(key);
  }
  auto it = lanes.mask.find(key);
  if (it == lanes.mask.end()) {
    lanes.mask.emplace(key, std::string{m});
    return;
  }
  auto av = Dlop::from_pyrope(it->second);
  if (av->is_invalid()) {
    lanes.dyn.insert(key);
    return;
  }
  if (detect && !av->and_op(*mv)->is_known_false()) {
    lanes.overlap.insert(key);
  }
  it->second = av->or_op(*mv)->to_pyrope();
}

}  // namespace

namespace {
// ── Multi-wire gatherers ─────────────────────────────────────────────────────
// One walk of a scope collects the wire metrics for EVERY wire at once, instead
// of the prp_*_wire helpers above which each re-walked the whole scope subtree
// PER wire (O(wires * nodes) -> O(nodes) per scope). Each gatherer mirrors the
// descent rules of its single-wire counterpart exactly (and reuses the same
// prp_wire_driver_store / prp_wire_nil_store predicates), so the diagnostics are
// unchanged — only their cost is.

// driver-store target name of `c` (empty if `c` is not a `store(ref, …)`).
std::string_view prp_store_ref_name(const Lnast& ln, const Lnast_nid& c) {
  if (!Lnast_ntype::is_store(ln.get_type(c))) {
    return {};
  }
  auto c0 = ln.get_first_child(c);
  if (c0.is_invalid() || !Lnast_ntype::is_ref(ln.get_type(c0))) {
    return {};
  }
  return ln.get_name(c0);
}

// Driver metrics belong to bindings, not spellings. A declaration in a
// nested scope starts a different binding (possibly before a later declaration
// in the enclosing scope), so its writes must not count against that scope.
struct Driver_scope {
  const Driver_scope*                   outer;
  absl::flat_hash_set<std::string_view> locals;

  Driver_scope(const Lnast& ln, const Lnast_nid& node, const Driver_scope* parent) : outer(parent) {
    if (outer && Lnast_ntype::is_stmts(ln.get_type(node))) {
      for (auto c : ln.children(node)) {
        if (Lnast_ntype::is_declare(ln.get_type(c))) {
          const auto name = ln.get_first_child(c);
          if (!name.is_invalid()) {
            locals.insert(ln.get_name(name));
          }
        }
      }
    }
  }

  bool hides(std::string_view name) const { return locals.contains(name) || (outer && outer->hides(name)); }
};

// Mirror of prp_wire_has_field_store, for all wires: a `store(w, 'field', v)`
// (>=3 children, const selector) anywhere except inside a func_def body.
void gather_field_store_wires(const Lnast& ln, const Lnast_nid& nid, absl::flat_hash_set<std::string>& out,
                              const Driver_scope* outer = nullptr) {
  const Driver_scope scope(ln, nid, outer);
  if (Lnast_ntype::is_func_def(ln.get_type(nid))) {
    return;
  }
  if (Lnast_ntype::is_store(ln.get_type(nid))) {
    auto c0 = ln.get_first_child(nid);
    if (!c0.is_invalid() && Lnast_ntype::is_ref(ln.get_type(c0))) {
      auto c1 = ln.get_sibling_next(c0);
      auto c2 = c1.is_invalid() ? c1 : ln.get_sibling_next(c1);
      if (!c2.is_invalid() && Lnast_ntype::is_const(ln.get_type(c1)) && !scope.hides(ln.get_name(c0))) {
        out.insert(std::string(ln.get_name(c0)));
      }
    }
  }
  for (auto c = ln.get_first_child(nid); !c.is_invalid(); c = ln.get_sibling_next(c)) {
    gather_field_store_wires(ln, c, out, &scope);
  }
}

// Mirror of prp_wire_written_under_loop, for all wires.
void gather_loop_driven_wires(const Lnast& ln, const Lnast_nid& nid, bool in_loop, absl::flat_hash_set<std::string>& out,
                              const Driver_scope* outer = nullptr) {
  const Driver_scope scope(ln, nid, outer);
  const auto         t = ln.get_type(nid);
  if (Lnast_ntype::is_stmts(t)) {
    for (auto c = ln.get_first_child(nid); !c.is_invalid(); c = ln.get_sibling_next(c)) {
      if (in_loop) {
        const auto w = prp_store_ref_name(ln, c);
        if (!w.empty() && !scope.hides(w) && prp_wire_driver_store(ln, c, w)) {
          out.insert(std::string(w));
        }
      }
      const auto ct = ln.get_type(c);
      if (Lnast_ntype::is_if_like(ct) || Lnast_ntype::is_for(ct) || Lnast_ntype::is_while(ct) || Lnast_ntype::is_tick(ct)
          || Lnast_ntype::is_stmts(ct)) {
        gather_loop_driven_wires(ln, c, in_loop, out, &scope);
      }
    }
    return;
  }
  if (Lnast_ntype::is_if_like(t) || Lnast_ntype::is_for(t) || Lnast_ntype::is_while(t) || Lnast_ntype::is_tick(t)) {
    const bool nl = in_loop || Lnast_ntype::is_for(t) || Lnast_ntype::is_while(t) || Lnast_ntype::is_tick(t);
    for (auto c = ln.get_first_child(nid); !c.is_invalid(); c = ln.get_sibling_next(c)) {
      if (Lnast_ntype::is_stmts(ln.get_type(c))) {
        gather_loop_driven_wires(ln, c, nl, out, &scope);
      }
    }
  }
}

// Mirror of prp_subtree_writes_wire, for all wires: every statement-level driver
// store anywhere in the control subtree. `full` collects the subset written by
// at least one WHOLE-value store (not a bit-range partial write) — see
// prp_wire_partial_store.
// Fold a nested construct's lane coverage into the enclosing one. `detect`
// false is the ARM case (mutually exclusive paths, so shared lanes are not a
// conflict); a nested scope's OWN conflicts always propagate.
void wire_lanes_merge(Wire_lanes& dst, const Wire_lanes& src, bool detect) {
  for (const auto& [w, m] : src.mask) {
    wire_lane_add(dst, w, m, detect);
  }
  for (const auto& w : src.dyn) {
    wire_lane_add(dst, w, "", detect);
  }
  for (const auto& w : src.overlap) {
    dst.overlap.insert(w);
  }
}

// `out` is the driver count along the WORST straight-line path through this
// subtree: writes in one block ADD, arms of one if/match take the MAX (they are
// mutually exclusive, so `if c { w = a } else { w = b }` is ONE driver while
// `if c { w = a; w = b }` is two — the shape a presence-set could not tell
// apart).
void gather_subtree_write_wires(const Lnast& ln, const Lnast_nid& nid, absl::flat_hash_map<std::string, int>& out,
                                absl::flat_hash_set<std::string>& full, Wire_lanes& lanes, const Driver_scope* outer = nullptr) {
  const Driver_scope scope(ln, nid, outer);
  const auto         t = ln.get_type(nid);
  if (Lnast_ntype::is_stmts(t)) {
    Lnast_nid prev;
    for (auto c = ln.get_first_child(nid); !c.is_invalid(); prev = c, c = ln.get_sibling_next(c)) {
      const auto w = prp_store_ref_name(ln, c);
      if (!w.empty() && !scope.hides(w) && prp_wire_driver_store(ln, c, w)) {
        ++out[std::string(w)];
        std::string m;
        if (!prp_wire_partial_store(ln, c, prev, w, &m)) {
          full.insert(std::string(w));
        } else {
          wire_lane_add(lanes, w, m, /*detect=*/true);  // same straight line: both writes run
        }
      }
      const auto ct = ln.get_type(c);
      if (Lnast_ntype::is_if_like(ct) || Lnast_ntype::is_for(ct) || Lnast_ntype::is_while(ct) || Lnast_ntype::is_tick(ct)) {
        Wire_lanes                            sub;  // ONE construct == one union over its arms
        absl::flat_hash_map<std::string, int> sub_count;
        gather_subtree_write_wires(ln, c, sub_count, full, sub, &scope);
        for (const auto& [ww, k] : sub_count) {
          out[ww] += k;  // the construct runs after the writes above it
        }
        wire_lanes_merge(lanes, sub, /*detect=*/true);
      } else if (Lnast_ntype::is_stmts(ct)) {
        gather_subtree_write_wires(ln, c, out, full, lanes, &scope);  // a plain block: same straight line
      }
    }
    return;
  }
  if (Lnast_ntype::is_if_like(t) || Lnast_ntype::is_for(t) || Lnast_ntype::is_while(t) || Lnast_ntype::is_tick(t)) {
    for (auto c = ln.get_first_child(nid); !c.is_invalid(); c = ln.get_sibling_next(c)) {
      if (Lnast_ntype::is_stmts(ln.get_type(c))) {
        Wire_lanes                            arm;
        absl::flat_hash_map<std::string, int> arm_count;
        gather_subtree_write_wires(ln, c, arm_count, full, arm, &scope);
        for (const auto& [ww, k] : arm_count) {
          auto& slot = out[ww];
          slot       = std::max(slot, k);  // arms never both run
        }
        wire_lanes_merge(lanes, arm, /*detect=*/false);
      }
    }
  }
}

// Mirror of prp_count_wire_drivers, for all wires in one straight-line pass over
// `stmts`. `count` = top-level driver-statement count; `full` = wires that have
// at least one WHOLE-value driver (so a >1 count on a wire absent from `full` is
// a bit-field assembly, not a multiple-driver error).
//
// 2c-wire — COVERAGE is deliberately NOT tracked: a wire driven on only SOME
// control paths is legal. Its one driver defines the net, so the write behaves
// as if it were unconditional (upass.tolg fills the unwritten paths with the
// written value — see lower_if's wire don't-care fill). What stays illegal is a
// SECOND driver, which is what `count`/`full`/`lanes` measure.
void gather_count_wire_drivers(const Lnast& ln, const Lnast_nid& stmts, absl::flat_hash_map<std::string, int>& count,
                               absl::flat_hash_set<std::string>& full, Wire_lanes& lanes, const Driver_scope* outer = nullptr) {
  const Driver_scope scope(ln, stmts, outer);
  Lnast_nid          prev;
  for (auto c = ln.get_first_child(stmts); !c.is_invalid(); prev = c, c = ln.get_sibling_next(c)) {
    const auto t = ln.get_type(c);
    const auto w = prp_store_ref_name(ln, c);
    if (!w.empty() && !scope.hides(w) && prp_wire_driver_store(ln, c, w)) {
      ++count[std::string(w)];
      std::string m;
      if (!prp_wire_partial_store(ln, c, prev, w, &m)) {
        full.insert(std::string(w));
      } else {
        wire_lane_add(lanes, w, m, /*detect=*/true);
      }
    } else if (Lnast_ntype::is_if_like(t)) {
      // An if/match contributes the driver count of its WORST arm: writing the
      // net once in each of two exclusive arms is ONE driver, writing it twice
      // inside one arm is two.
      absl::flat_hash_map<std::string, int> writes;
      Wire_lanes                            arm_lanes;
      gather_subtree_write_wires(ln, c, writes, full, arm_lanes, &scope);
      for (const auto& [ww, k] : writes) {
        count[ww] += k;
      }
      // The construct's lanes, unioned across its arms, now meet the lanes this
      // straight-line scope already drives — and THERE an overlap is real.
      wire_lanes_merge(lanes, arm_lanes, /*detect=*/true);
      // A per-arm rescan only to propagate the arm's OWN lane conflicts (an arm
      // that double-drives the same bits is a conflict wherever it sits).
      for (auto a = ln.get_first_child(c); !a.is_invalid(); a = ln.get_sibling_next(a)) {
        if (!Lnast_ntype::is_stmts(ln.get_type(a))) {
          continue;
        }
        absl::flat_hash_map<std::string, int> ac;
        Wire_lanes                            al;
        gather_count_wire_drivers(ln, a, ac, full, al, &scope);
        for (const auto& ww : al.overlap) {
          lanes.overlap.insert(ww);
        }
      }
    } else if (Lnast_ntype::is_stmts(t)) {
      absl::flat_hash_map<std::string, int> nc;
      gather_count_wire_drivers(ln, c, nc, full, lanes, &scope);  // a plain block: same straight line
      for (const auto& [ww, k] : nc) {
        count[ww] += k;
      }
    }
  }
}
}  // namespace

void Prp2lnast::check_wire_drivers() const {
  check_wire_scope(lnast->get_root());

  // A loop body may READ a `wire` but never DRIVE one (docs 05b-statements.md
  // "Loops and `wire`"): the body runs once per iteration, so a write there
  // adds one driver per iteration and breaks the single-driver rule. A
  // body-local `wire w = expr` drives it too. This is a SOURCE legality rule,
  // independent of the roll knob; a loop whose body reads a wire is unrolled
  // (uPass_runner::plan_loop_roll) unless a runtime `break`/`continue` leaves
  // it no unrolled form, so rolled and unrolled lowerings never disagree about
  // what compiles.
  const auto                            root = lnast->get_root();
  std::function<void(const Lnast_nid&)> check_unit;
  check_unit = [&](const Lnast_nid& unit) {
    const auto add_if_wire_decl = [&](const Lnast_nid& nid, absl::flat_hash_set<std::string>& out) {
      if (!Lnast_ntype::is_declare(lnast->get_type(nid))) {
        return;
      }
      auto name_n = lnast->get_first_child(nid);
      auto type_n = name_n.is_invalid() ? name_n : lnast->get_sibling_next(name_n);
      auto mode_n = type_n.is_invalid() ? type_n : lnast->get_sibling_next(type_n);
      if (!name_n.is_invalid() && !mode_n.is_invalid() && Lnast_ntype::is_const(lnast->get_type(mode_n))) {
        const auto mode = lnast->get_name(mode_n);
        if (mode == "wire" || mode.starts_with("wire ")) {
          out.emplace(lnast->get_name(name_n));
        }
      }
    };
    // Every wire declared anywhere under `root` (any depth). Used for the loop
    // BODY: a body-local wire is not visible to the enclosing scope, and its
    // declaration-drive (`wire w = expr`) is a drive inside the loop.
    std::function<void(const Lnast_nid&, absl::flat_hash_set<std::string>&)> gather_subtree
        = [&](const Lnast_nid& nid, absl::flat_hash_set<std::string>& out) {
            if (nid.is_invalid() || Lnast_ntype::is_func_def(lnast->get_type(nid))) {
              return;
            }
            add_if_wire_decl(nid, out);
            for (auto c : lnast->children(nid)) {
              gather_subtree(c, out);
            }
          };

    // The first store under `nid` that drives one of `wires`: a whole write, a
    // field write, or the write-back half of a bit-range write (the same
    // driver shape the single-driver count uses). `= nil` only forward-declares
    // a wire declared in the body itself; on a wire declared outside the loop
    // (`outer`) it is a write like any other. A named actual or a tuple field
    // label (a `store` directly under a call/tuple_add) is a structural key,
    // not a write; every other use of a wire is a legal read.
    std::function<std::optional<std::pair<Lnast_nid, std::string>>(const Lnast_nid&,
                                                                   bool,
                                                                   const absl::flat_hash_set<std::string>&,
                                                                   const absl::flat_hash_set<std::string>&)>
        find_drive;
    find_drive = [&](const Lnast_nid&                        nid,
                     bool                                    structural_key,
                     const absl::flat_hash_set<std::string>& wires,
                     const absl::flat_hash_set<std::string>& outer) -> std::optional<std::pair<Lnast_nid, std::string>> {
      if (nid.is_invalid() || Lnast_ntype::is_func_def(lnast->get_type(nid))) {
        return std::nullopt;
      }
      const auto type = lnast->get_type(nid);
      if (!structural_key) {
        const auto target = prp_store_ref_name(*lnast, nid);
        if (!target.empty() && wires.contains(target) && (outer.contains(target) || prp_wire_driver_store(*lnast, nid, target))) {
          // The statement carries the source span (a leaf ref often does not),
          // so line-pinned diagnostics stay stable as the file changes.
          return std::pair{nid, std::string(target)};
        }
      }
      const bool keys = Lnast_ntype::is_func_call(type) || Lnast_ntype::is_tuple_add(type) || Lnast_ntype::is_tuple_concat(type);
      for (auto c : lnast->children(nid)) {
        if (auto found = find_drive(c, keys, wires, outer); found) {
          return found;
        }
      }
      return std::nullopt;
    };

    // Scope-aware walk. A `wire` is visible to the `stmts` scope that declares
    // it and to the scopes NESTED inside it — never to a SIBLING scope. The
    // earlier unit-wide gather rejected
    //     if c { wire w:u8; w = a }
    //     for i in 0..<4 { mut w:u8 = i; s = s + w }
    // where the loop's `w` is an ordinary local that merely shares a name with a
    // wire in an unrelated arm (legal today: siblings are not shadowing).
    std::function<void(const Lnast_nid&, const absl::flat_hash_set<std::string>&)> scan;
    scan = [&](const Lnast_nid& nid, const absl::flat_hash_set<std::string>& outer) {
      if (nid.is_invalid() || (nid != unit && Lnast_ntype::is_func_def(lnast->get_type(nid)))) {
        return;
      }
      absl::flat_hash_set<std::string>        scoped;
      const absl::flat_hash_set<std::string>* visible = &outer;
      if (Lnast_ntype::is_stmts(lnast->get_type(nid))) {
        scoped = outer;
        for (auto c : lnast->children(nid)) {
          add_if_wire_decl(c, scoped);
        }
        visible = &scoped;
      }
      const auto type = lnast->get_type(nid);
      // `while` covers BOTH `while cond { … }` and `loop { … }` (process_loop_statement
      // desugars to the same node). The rule above is a SOURCE legality rule for loop
      // bodies, so checking only `for` made the identical construct legal or illegal
      // depending on how the loop was spelled.
      if (Lnast_ntype::is_for(type) || Lnast_ntype::is_while(type)) {
        for (auto body : lnast->children(nid)) {
          if (!Lnast_ntype::is_stmts(lnast->get_type(body))) {
            continue;  // for: value/iterable/mode/idx/key -- while: the cond
          }
          auto body_wires = *visible;
          gather_subtree(body, body_wires);
          if (auto drive = find_drive(body, false, body_wires, *visible); drive) {
            report_error(
                drive->first,
                "wire-in-loop",
                "name",
                std::format("{} body may not drive wire '{}'", Lnast_ntype::is_for(type) ? "for-loop" : "loop", drive->second),
                "drive the wire outside the loop; a loop body may only read a wire");
          }
        }
      }
      for (auto c : lnast->children(nid)) {
        scan(c, *visible);
      }
    };
    scan(unit, {});

    // Nested definitions own independent scopes and may reuse wire names.
    std::function<void(const Lnast_nid&)> nested = [&](const Lnast_nid& nid) {
      for (auto c : lnast->children(nid)) {
        if (Lnast_ntype::is_func_def(lnast->get_type(c))) {
          check_unit(c);
        } else {
          nested(c);
        }
      }
    };
    nested(unit);
  };
  check_unit(root);
}

void Prp2lnast::check_wire_scope(const Lnast_nid& node) const {
  // At each `stmts` scope, enforce the single-driver rules on every `wire` — and
  // the single-BIND rule on every `const` — declared directly in it (their
  // drivers live in this scope; the count recurses into nested if/match and
  // plain blocks).
  if (Lnast_ntype::is_stmts(lnast->get_type(node))) {
    // Collect the wires declared directly in this scope first, then gather every
    // wire's driver metrics in ONE walk of the scope (was a full-subtree walk per
    // wire — O(wires * nodes); see the gatherers above).
    std::vector<std::pair<Lnast_nid, std::string_view>> scope_wires;
    // `const` names declared in this scope. A `const` is SINGLE-ASSIGNMENT, so
    // the same driver count applies — but only the >1 half: a `const x = nil`
    // that is never bound is a legal "no value yet" declaration (reading it is
    // what errors), so there is no undriven diagnostic here. This catches the
    // RUNTIME rebind the comptime `vbound` tally in upass.attributes cannot see.
    std::vector<std::pair<Lnast_nid, std::string_view>> scope_consts;
    for (auto c = lnast->get_first_child(node); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (!Lnast_ntype::is_declare(lnast->get_type(c))) {
        continue;
      }
      auto name_n = lnast->get_first_child(c);
      auto type_n = name_n.is_invalid() ? name_n : lnast->get_sibling_next(name_n);
      auto mode_n = type_n.is_invalid() ? type_n : lnast->get_sibling_next(type_n);
      if (mode_n.is_invalid() || !Lnast_ntype::is_const(lnast->get_type(mode_n))) {
        continue;
      }
      const auto mode = lnast->get_name(mode_n);
      if (mode == "const" || mode.starts_with("const ")) {
        scope_consts.emplace_back(c, lnast->get_name(name_n));
        continue;
      }
      if (mode != "wire" && !mode.starts_with("wire ")) {
        continue;
      }
      scope_wires.emplace_back(c, lnast->get_name(name_n));
    }
    if (!scope_wires.empty() || !scope_consts.empty()) {
      // The names whose declaration in THIS scope emitted a shape seed.
      absl::flat_hash_set<std::string> seeded;
      for (auto c = lnast->get_first_child(node); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
        if (decl_shape_seed_stores_.contains(c)) {
          if (const auto t = lnast->get_first_child(c); !t.is_invalid()) {
            seeded.insert(std::string(lnast->get_name(t)));
          }
        }
      }
      absl::flat_hash_set<std::string>      field_wires;
      absl::flat_hash_set<std::string>      loop_wires;
      absl::flat_hash_map<std::string, int> driver_count;
      absl::flat_hash_set<std::string>      full_driven_wires;
      Wire_lanes                            lanes;
      gather_field_store_wires(*lnast, node, field_wires);
      gather_loop_driven_wires(*lnast, node, /*in_loop=*/false, loop_wires);
      gather_count_wire_drivers(*lnast, node, driver_count, full_driven_wires, lanes);
      for (const auto& [c, wname] : scope_wires) {
        // A tuple-bundle wire (`wire io:(a,b)` driven by per-field `io.a = …`) is
        // single-driver PER LEAF; the base-level count is meaningless. detuple
        // splits it into per-leaf `wire io.a` nets, each checked downstream.
        if (field_wires.contains(wname)) {
          continue;
        }
        // A wire driven inside a loop is unrolled later — its multiplicity is
        // unknowable here; defer to tolg's post-unroll checks.
        if (loop_wires.contains(wname)) {
          continue;
        }
        const auto dit     = driver_count.find(wname);
        int        drivers = dit == driver_count.end() ? 0 : dit->second;
        // Discount the inline-tuple TYPE's shape-seed store (see
        // decl_shape_seed_stores_): it is a compiler artifact of the type, not
        // one of the net's drivers.
        if (drivers > 0 && seeded.contains(wname)) {
          --drivers;
          if (drivers <= 1) {
            full_driven_wires.erase(wname);  // the seed was the only WHOLE store
          }
        }
        // A net ASSEMBLED from DISJOINT bit fields (`w#[0..=3] = a;
        // w#[4..=7] = b`) has many write statements but ONE driver — each
        // refines the same value, and upass.tolg accumulates the whole chain
        // on one din shadow. What makes a second driver is a WHOLE-value store
        // — or partial writes whose LANES are not disjoint: tolg chains those
        // on the same accumulator too, so the later write silently wins over
        // the shared bits. Disjointness is the whole justification for the
        // exemption, so it has to be checked, not assumed.
        if (drivers > 1 && (full_driven_wires.contains(wname) || lanes.overlap.contains(std::string(wname)))) {
          report_error(c,
                       "wire-multiple-drivers",
                       "name",
                       std::format("wire '{}' has more than one driver — a `wire` is a single-driver net", wname),
                       "give it exactly one assignment (one `if`/`match` counts as a single driver)");
        } else if (drivers == 0) {
          // A CONDITIONAL single driver is legal: the write may sit inside an
          // `if`/`match` with no `else`. Wherever it lands it is still the net's
          // ONE driver, so the wire carries that value on every path — the same
          // as if the assignment had been written unconditionally. Only "no
          // driver at all" is an error here.
          report_error(c,
                       "wire-undriven",
                       "name",
                       std::format("wire '{}' is declared but never driven", wname),
                       "assign it (a `wire` must have exactly one driver), or remove the declaration");
        }
      }
      // A name declared twice in one scope is a redeclaration semacheck
      // reports. The driver count cannot be attributed to either declaration.
      // Initializer locals have separate scopes and do not need this exemption.
      absl::flat_hash_set<std::string> redeclared_consts;
      {
        absl::flat_hash_set<std::string> seen;
        for (const auto& [c, cname] : scope_consts) {
          (void)c;
          if (!seen.insert(std::string(cname)).second) {
            redeclared_consts.insert(std::string(cname));
          }
        }
      }
      for (const auto& [c, cname] : scope_consts) {
        // Same exemptions as a wire: a per-field bundle bind is one bind per
        // LEAF, and a loop's writes are unrolled later.
        if (field_wires.contains(cname) || loop_wires.contains(cname) || redeclared_consts.contains(cname)) {
          continue;
        }
        const auto dit     = driver_count.find(cname);
        int        drivers = dit == driver_count.end() ? 0 : dit->second;
        // A typed tuple's shape-seed store (`const k:(a:u6, b:bool) = …`) is
        // the TYPE's layout, not a bind -- the same discount as for a wire.
        if (drivers > 0 && seeded.contains(cname)) {
          --drivers;
          if (drivers <= 1) {
            full_driven_wires.erase(cname);
          }
        }
        // Disjoint bit-field assembly is ONE bind, exactly as for a wire.
        if (drivers > 1 && (full_driven_wires.contains(cname) || lanes.overlap.contains(std::string(cname)))) {
          report_error(c,
                       "const-rebind",
                       "name",
                       std::format("const '{}' rebind — a `const` is assigned exactly once", cname),
                       "declare it `mut` for last-write-wins, or give it a single assignment (one `if`/`match` counts "
                       "as one)");
        }
      }
    }
  }
  for (auto c = lnast->get_first_child(node); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    check_wire_scope(c);
  }
}

void Prp2lnast::dump_tree_sitter() const { dump_tree_sitter(ts_root_node, 1); }

void Prp2lnast::dump_tree_sitter(TSNode n, int level) const {
  if (ts_node_is_null(n)) {
    return;
  }
  auto             indent       = std::string(level * 2, ' ');
  auto             num_children = ts_node_child_count(n);
  std::string_view node_type(ts_node_type(n));

  std::print("{}{} {}\n", indent, node_type, num_children);

  for (uint32_t i = 0; i < num_children; ++i) {
    dump_tree_sitter(ts_node_child(n, i), level + 1);
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

// 2f-stream top-level driver: lower ONE construct pulled from the parse stream.
// Mirrors the lower_children_range loop body for the file scope — the hidden
// wrap/sat overflow-prefix gap scan + process_statement — but NOT the early-
// `return` rewrite, which is inert at file scope (`return` is only valid inside a
// function body, where return_flag_name_ is set; the nested scopes that can carry
// a return still go through lower_children_range, which keeps the rewrite).
void Prp2lnast::lower_streamed_top_level(TSNode c, std::string_view& pending_overflow, uint32_t& prev_end) {
  using namespace std::string_view_literals;
  std::string_view t(ts_node_type(c));

  if (!ts_node_is_named(c)) {  // defensive: the stream yields named statements
    if (t == "wrap"sv || t == "sat"sv) {
      pending_overflow = t;
    }
    return;
  }
  // A scope_statement already consumed as a lambda body must not re-walk (a
  // tree-sitter-era shape; prpparse attaches the body to the lambda, so this is
  // inert here — kept for parity with lower_children_range).
  if (consumed_lambda_body_starts.contains(ts_node_start_byte(c))) {
    pending_overflow = {};
    prev_end         = ts_node_end_byte(c);
    return;
  }
  const bool is_assign = (t == "assignment"sv);
  if (is_assign && pending_overflow.empty()) {
    pending_overflow = scan_overflow_in_gap(prev_end, ts_node_start_byte(c));
  }
  if (is_assign && !pending_overflow.empty()) {
    pending_overflow_kind = pending_overflow;
  }
  process_statement(c);
  pending_overflow_kind = {};
  pending_overflow      = {};
  prev_end              = ts_node_end_byte(c);
}

void Prp2lnast::process_description() {
  builder.idx_stmts = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
  // 2f-stream: stream the file's top-level constructs. Each parse_next() yields
  // one construct and recycles the previous one's CST arena, so the whole parse
  // tree never exists at once. Nested scopes within a construct still lower via
  // walk_statement_block/lower_children_range (the construct is fully alive
  // during its own walk).
  std::string_view pending_overflow;
  uint32_t         prev_end = 0;  // start of buffer (matches lower_children_range's from==0)
  while (prpparse::Ast* c_ast = prp_parser->parse_next()) {
    lower_streamed_top_level(TSNode{c_ast, prp_buf.get()}, pending_overflow, prev_end);
  }
  finalize_current_lnast();
}

void Prp2lnast::finalize_current_lnast() {
  // Each destination owns its own builder/counter, so close and stabilize it
  // as soon as its lambda ends instead of waiting for the source file.
  builder.stabilize_fallback_tmps();
  // Run the scope checks on the PRE-rewrite tree: declarations are still in
  // their `attr_set(t,"type",K)` form here, with their source spans intact and
  // each scope's declarations un-merged. rewrite_decls_to_declare folds
  // same-name declaration clusters and does not preserve declare-node locs, so
  // running the no-shadowing / undeclared / undefined-read checks afterward
  // would both lose the diagnostic location and miss a `for c` iterator that
  // shadows an outer `mut c` (the merge collapses them).
  check_undeclared_writes();
  check_undefined_reads();
  rewrite_decls_to_declare();
  // 2c-wire — enforce the single-driver rules AFTER the decl-merge (so each
  // `wire` is a `declare(name, type, "wire")`) but on the pre-elaborate tree
  // (before lnastfmt drops a dead first write, which would hide a double-drive).
  check_wire_drivers();
}

void Prp2lnast::push_streamed_destination(std::string_view name, std::string_view kind, bool timecheck_off,
                                          std::string_view lg_name) {
  destination_stack_.push_back(Destination_state{.lnast                  = std::move(lnast),
                                                 .builder                = std::move(builder),
                                                 .read_sites             = std::move(read_sites_),
                                                 .read_scope_decls       = std::move(read_scope_decls_),
                                                 .read_child_index       = std::move(read_child_index_),
                                                 .tick_loop_var_decls    = std::move(tick_loop_var_decls_),
                                                 .decl_shape_seed_stores = std::move(decl_shape_seed_stores_),
                                                 .streamed_scope_names   = std::move(streamed_scope_names_),
                                                 .capture_rw             = std::move(capture_rw_)});
  lnast = std::make_shared<Lnast>(name);
  lnast->set_root(Lnast_ntype::create_top());
  lnast->set_lambda_kind(kind);
  // `::[timecheck=false]` relaxes TIMING only; it never marks Pyrope as
  // Verilog-origin (user ruling 2026-09-28 (22)).
  lnast->set_skip_timecheck(timecheck_off);
  if (!lg_name.empty()) {
    lnast->set_lg_name(lg_name);
  }
  if (!src_relpath.empty()) {
    lnast->source_locator().set_file_content(src_relpath, std::string(prp_file));
  }
  builder       = Lnast_builder{};
  builder.lnast = lnast;
  read_sites_.clear();
  read_scope_decls_.clear();
  read_child_index_.clear();
  tick_loop_var_decls_.clear();
  decl_shape_seed_stores_.clear();
  streamed_scope_names_.clear();
  capture_rw_.clear();
}

std::shared_ptr<Lnast> Prp2lnast::pop_streamed_destination() {
  I(!destination_stack_.empty());
  finalize_current_lnast();
  auto completed = std::move(lnast);
  auto state     = std::move(destination_stack_.back());
  destination_stack_.pop_back();
  lnast                   = std::move(state.lnast);
  builder                 = std::move(state.builder);
  read_sites_             = std::move(state.read_sites);
  read_scope_decls_       = std::move(state.read_scope_decls);
  read_child_index_       = std::move(state.read_child_index);
  tick_loop_var_decls_    = std::move(state.tick_loop_var_decls);
  decl_shape_seed_stores_ = std::move(state.decl_shape_seed_stores);
  streamed_scope_names_   = std::move(state.streamed_scope_names);
  capture_rw_             = std::move(state.capture_rw);
  return completed;
}

namespace {
// Copy one LNAST node (preserving ref/const text) under dst_parent.
Lnast_nid prp_copy_one_node(const Lnast& src, const Lnast_nid& src_nid, Lnast& dst, const Lnast_nid& dst_parent) {
  const auto t  = src.get_type(src_nid);
  Lnast_nid  nn = dst.add_child(dst_parent, t);
  if (Lnast_ntype::is_ref(t) || Lnast_ntype::is_const(t)) {
    dst.set_name_id(nn, src.get_name_id(src_nid));
  }
  // Carry the SourceId across the decl-merge rebuild — one integer attr,
  // copied unconditionally for every def-bearing kind (the dst tree
  // becomes this same Lnast's body via replace_body, so the id stays
  // resolvable in the Lnast's own locator).
  if (Lnast::srcid_carries(t)) {
    dst.set_srcid(nn, src.get_srcid(src_nid));
  }
  return nn;
}

// Comment/whitespace-insensitive fingerprint of a source span. The streamed
// lambda marker rides this in the wrapper tree, where it participates in the
// compile cache's semantic hash: hashing the RAW bytes made a comment (or
// reindent) INSIDE a lambda body defeat the comment-only warm hit that
// semantic_identical provides everywhere else.
//
// The span is re-lexed with prpparse's own Lexer, so what counts as a comment is
// exactly what the parser skipped: a `//` or `/*` inside a string ('…' has no
// escapes, "…" has escapes and `{…}` holes that may hold nested strings) or a
// backtick name is a real token byte, never stripped (a hand-rolled scanner got
// `'\'` and `"{"a" ++ "//2"}"` wrong and reused a stale netlist). The hash
// covers every token's bytes (the hidden `wrap`/`sat` prefixes are ordinary
// tokens) plus one separator per inter-token gap: '\n' when the gap holds a
// newline outside its comments (statement boundaries), ' ' for any other
// whitespace/comment, nothing for adjacent tokens. A comment inside a string
// hole is part of that string token's bytes (a miss, never a stale hit).
uint64_t stable_source_fingerprint(std::string_view txt) {
  std::string norm;
  norm.reserve(txt.size());
  try {
    const prpparse::Source_buffer buf("", std::string(txt));
    prpparse::Lexer               lex(buf);
    const auto                    toks = lex.tokenize();
    const auto&                   cmts = lex.comments();  // in source order
    size_t                        ci   = 0;
    uint32_t                      prev = 0;
    for (const auto& tok : toks) {
      const uint32_t gs = prev;
      const uint32_t ge = tok.start_byte;
      if (ge > gs) {
        bool nl = false;
        for (uint32_t k = gs; k < ge;) {
          while (ci < cmts.size() && cmts[ci].end_byte <= k) {
            ++ci;
          }
          if (ci < cmts.size() && cmts[ci].start_byte <= k) {
            k = cmts[ci].end_byte;  // inside a comment: its newlines do not count
            continue;
          }
          if (txt[k] == '\n') {
            nl = true;
            break;
          }
          ++k;
        }
        norm.push_back(nl ? '\n' : ' ');
      }
      norm.append(txt.substr(tok.start_byte, tok.end_byte - tok.start_byte));
      norm.push_back('\0');  // token boundary: `ab` != `a` `b` even when adjacent
      prev = tok.end_byte;
    }
  } catch (...) {
    // Not lexable on its own (should not happen for a parsed span): hash the
    // raw bytes — a comment edit then misses the cache, which is safe.
    norm.assign("\x01raw:");
    norm.append(txt);
  }
  return std::hash<std::string>{}(norm);
}

// Deep-copy a prpparse CST subtree into `dst` (2f-stream). The streaming parser
// recycles its arena per construct, so a CST node a later statement still needs
// (an `enum(...NAME)` spread's const rvalue) is cloned into a persistent arena.
// Byte offsets are preserved — they index the still-live source buffer — so
// get_text() on the clone reads the same bytes. Call prpparse::link_parents on
// the result before facade navigation.
prpparse::Ast* clone_prp_subtree(prpparse::Ast_arena& dst, const prpparse::Ast* src) {
  if (src == nullptr) {
    return nullptr;
  }
  prpparse::Ast* c = dst.make(src->kind, src->start_byte, src->end_byte);
  c->field         = src->field;
  c->named         = src->named;
  for (const prpparse::Ast* k : src->kids) {
    c->add(clone_prp_subtree(dst, k));
  }
  return c;
}

// Faithful recursive subtree copy (no merging) — used for the TYPE subtree.
void prp_copy_subtree(const Lnast& src, const Lnast_nid& src_nid, Lnast& dst, const Lnast_nid& dst_parent) {
  auto nn = prp_copy_one_node(src, src_nid, dst, dst_parent);
  for (auto c : src.children(src_nid)) {
    prp_copy_subtree(src, c, dst, nn);
  }
}

// Name of the Nth child of `nid` (0-based), or "" if absent.
std::string_view prp_child_name(const Lnast& src, const Lnast_nid& nid, int n) {
  int i = 0;
  for (auto c : src.children(nid)) {
    if (i == n) {
      return src.get_name(c);
    }
    ++i;
  }
  return {};
}
// The Nth child nid, or invalid.
Lnast_nid prp_child_nid(const Lnast& src, const Lnast_nid& nid, int n) {
  int i = 0;
  for (auto c : src.children(nid)) {
    if (i == n) {
      return c;
    }
    ++i;
  }
  return Lnast_nid{};
}
}  // namespace

void Prp2lnast::rewrite_decls_to_declare() {
  auto staging = std::make_shared<Lnast>(lnast->forest()->create_tree_temp("decl-merge"), lnast->get_top_module_name());

  // Recursive copy; at each `stmts` block, fold the declaration cluster
  // (attr_set(t,"type",K) [+ attr_set(t,"comptime")] [+ type_spec(t,TYPE)])
  // into one `declare(ref(t), TYPE|none_type, const(mode))`. Everything else —
  // including the value `store`, the `typename` attr_set, and nested blocks —
  // is copied verbatim (recursing so nested stmts merge too).
  std::function<Lnast_nid(const Lnast_nid&, const Lnast_nid&)> copy_merge
      = [&](const Lnast_nid& src_nid, const Lnast_nid& dst_parent) -> Lnast_nid {
    auto nn   = prp_copy_one_node(*lnast, src_nid, *staging, dst_parent);
    auto type = lnast->get_type(src_nid);
    if (!Lnast_ntype::is_stmts(type)) {
      for (auto c : lnast->children(src_nid)) {
        copy_merge(c, nn);
      }
      return nn;
    }
    // stmts block — scan children with cluster lookahead.
    std::vector<Lnast_nid> kids;
    for (auto c : lnast->children(src_nid)) {
      kids.push_back(c);
    }
    auto is_type_attr_set = [&](const Lnast_nid& k) {
      return Lnast_ntype::is_attr_set(lnast->get_type(k)) && prp_child_name(*lnast, k, 1) == "type";
    };
    // `reg x:T:[latch=true]` spells a level-sensitive LATCH (the grammar has no
    // `latch` declaration keyword; the prp_writer re-emits a latch this way).
    // Collect the flagged targets so the declare below gets mode "latch" (the
    // shape the slang reader emits and tolg lowers to Ntype Latch), and drop
    // the marker attr_set itself.
    auto is_latch_attr_set = [&](const Lnast_nid& k) {
      return Lnast_ntype::is_attr_set(lnast->get_type(k)) && prp_child_name(*lnast, k, 1) == "latch";
    };
    std::set<std::string> latch_targets;
    for (const auto& k : kids) {
      if (is_latch_attr_set(k)) {
        latch_targets.insert(std::string(prp_child_name(*lnast, k, 0)));
      }
    }
    for (size_t i = 0; i < kids.size();) {
      const auto& k = kids[i];
      if (is_latch_attr_set(k)) {
        ++i;  // consumed into the declare's mode — do not copy
        continue;
      }
      if (!is_type_attr_set(k)) {
        copy_merge(k, nn);
        ++i;
        continue;
      }
      // Cluster head: attr_set(t, "type", KIND).
      std::string target(prp_child_name(*lnast, k, 0));
      std::string kind(prp_child_name(*lnast, k, 2));
      bool        comptime  = false;
      Lnast_nid   type_node = {};
      size_t      j         = i + 1;
      if (j < kids.size() && Lnast_ntype::is_attr_set(lnast->get_type(kids[j])) && prp_child_name(*lnast, kids[j], 1) == "comptime"
          && prp_child_name(*lnast, kids[j], 0) == target) {
        comptime = true;
        ++j;
      }
      if (j < kids.size() && Lnast_ntype::is_type_spec(lnast->get_type(kids[j])) && prp_child_name(*lnast, kids[j], 0) == target) {
        type_node = kids[j];
        ++j;
      }
      // Emit declare(ref(t), TYPE|none_type, const(mode)).
      auto d = staging->add_child(nn, Lnast_ntype::create_declare());
      // Carry the cluster head's SourceId (attached at the attr_set creation)
      // so declaration-site diagnostics stay located post-merge.
      staging->set_srcid(d, lnast->get_srcid(k));
      staging->add_child(d, Lnast_node::create_ref(target));
      Lnast_nid tnid = type_node.is_invalid() ? Lnast_nid{} : prp_child_nid(*lnast, type_node, 1);
      if (!tnid.is_invalid()) {
        prp_copy_subtree(*lnast, tnid, *staging, d);
      } else {
        staging->add_child(d, Lnast_ntype::create_prim_type_none());
      }
      // A `stage[N]` decl lowers to a `reg` declare carrying a
      // trailing stages(min,max) node (the upass/pipe insertion shape): tolg
      // lowers it as one depth-N pipeline Flop (pipe_min/pipe_max pins).
      const bool  is_stage = kind == "stage";
      std::string mode(is_stage ? "reg" : kind);
      if (kind == "reg" && latch_targets.count(target)) {
        mode = "latch";  // `reg x:[latch=true]` — the latch spelling (see above)
      }
      if (comptime) {
        if (!mode.empty()) {
          mode.push_back(' ');
        }
        mode += "comptime";
      }
      staging->add_child(d, Lnast_node::create_const(mode));
      if (is_stage) {
        // The (min,max) rides the cluster head as its trailing child.
        auto st = prp_child_nid(*lnast, k, 3);
        if (!st.is_invalid() && Lnast_ntype::is_stages(lnast->get_type(st))) {
          prp_copy_subtree(*lnast, st, *staging, d);
        }
      } else if (kind == "reg") {
        // The reg initializer rides the cluster head (child 3);
        // fold it into the declare's optional 4th [value] child so tolg can
        // recover the reset value and the runner never binds it as a comptime
        // value (reg reads are runtime q reads).
        auto iv = prp_child_nid(*lnast, k, 3);
        if (!iv.is_invalid()) {
          prp_copy_subtree(*lnast, iv, *staging, d);
        }
      }
      i = j;
    }
    return nn;
  };

  auto src_root = lnast->get_root();
  auto dst_root = staging->set_root(lnast->get_type(src_root));
  // The streamed lambda root is its module-level source-map anchor.  The
  // declaration rewrite replaces the whole body, so carry that anchor just
  // as we carry the child statement ids below.
  staging->set_srcid(dst_root, lnast->get_srcid(src_root));
  for (auto c : lnast->children(src_root)) {
    copy_merge(c, dst_root);
  }
  lnast->replace_body(staging);  // adopts staging's name pool with the body
}

// True if `s` is a bare `return` (control_statement wrapping return_statement).
static bool ts_is_bare_return(TSNode s) {
  if (std::string_view(ts_node_type(s)) != "control_statement") {
    return false;
  }
  TSNode inner = ts_node_named_child(s, 0);
  return !ts_node_is_null(inner) && std::string_view(ts_node_type(inner)) == "return_statement";
}

// True if a `scope_statement`'s last statement is a bare `return` (i.e. the
// block returns on its straight-line tail). Minimal all-paths-return check —
// deeper nesting (return only inside a nested if of the tail) is not modeled.
static bool ts_block_tail_returns(TSNode scope) {
  if (ts_node_is_null(scope) || std::string_view(ts_node_type(scope)) != "scope_statement") {
    return false;
  }
  uint32_t n = ts_node_named_child_count(scope);
  return n > 0 && ts_is_bare_return(ts_node_named_child(scope, n - 1));
}

// True if `n`'s subtree contains a bare `return`, NOT crossing into a nested
// `lambda` (an inner function's return is its own concern).
static bool ts_subtree_has_return(TSNode n) {
  if (std::string_view(ts_node_type(n)) == "lambda") {
    return false;
  }
  if (ts_is_bare_return(n)) {
    return true;
  }
  for (TSNode c : ts_node_named_children(n)) {
    if (ts_subtree_has_return(c)) {
      return true;
    }
  }
  return false;
}

// True if `n`'s subtree contains a `return` lexically INSIDE a for/while/loop
// (not crossing a nested lambda). Such a function needs the synthesized
// return-flag desugar (a return can't be lowered as a pure scope-rewrite once
// it has to cross a loop boundary — it must stop the loop and skip past it).
static bool ts_subtree_has_loop_return(TSNode n, bool inside_loop) {
  std::string_view t(ts_node_type(n));
  if (t == "lambda") {
    return false;
  }
  if (ts_is_bare_return(n)) {
    return inside_loop;
  }
  const bool now_loop = inside_loop || t == "for_statement" || t == "while_statement" || t == "loop_statement";
  for (TSNode c : ts_node_named_children(n)) {
    if (ts_subtree_has_loop_return(c, now_loop)) {
      return true;
    }
  }
  return false;
}

// Recognize the early-return guard idiom `if cond { …; return }` — a plain
// (non-`unique`, single-arm, no `elif`, no `else`, no header `init`) `if` whose
// then-branch returns on its tail. Such a guard desugars structurally (no
// runtime flag): the statements after it move into a synthesized `else`, so the
// existing if-scope machinery handles the early termination (the tail-return
// statement itself is dropped). Returns the condition + then-body nodes.
bool Prp2lnast::is_guarded_return_if(TSNode s, TSNode& cond_out, TSNode& then_out) {
  if (std::string_view(ts_node_type(s)) != "if_expression") {
    return false;
  }
  const uint32_t nc = child_count(s);
  TSNode         cond{};
  TSNode         code{};
  int            nconds = 0, ncodes = 0, nelse = 0, ninit = 0;
  bool           is_unique = false;
  for (uint32_t i = 0; i < nc; i++) {
    TSNode c = child(s, i);
    if (trim(get_text(c)) == "unique") {  // anonymous `unique` marker (text, not node type)
      is_unique = true;
    }
    const char* fn = ts_node_field_name_for_child(s, i);
    if (!fn) {
      continue;
    }
    std::string_view f(fn);
    if (f == "condition") {
      cond = c;
      ++nconds;
    } else if (f == "code") {
      code = c;
      ++ncodes;
    } else if (f == "else") {
      ++nelse;  // tree-sitter tags BOTH the `else` keyword and its body
    } else if (f == "init") {
      ++ninit;
    }
  }
  if (is_unique || nconds != 1 || ncodes != 1 || nelse != 0 || ninit != 0) {
    return false;
  }
  if (!ts_block_tail_returns(code)) {
    return false;
  }
  cond_out = cond;
  then_out = code;
  return true;
}

void Prp2lnast::walk_statement_block(TSNode parent) { lower_children_range(parent, 0); }

// Lower a scope's child statements starting at child index `from`. Besides the
// normal per-statement lowering (with the hidden `wrap`/`sat` overflow-prefix
// gap-scan), this honors early `return` by restructuring the scope (per the
// 2f-return_leak design): a bare `return` drops the rest of the scope, and a
// guarded `if cond { … return }` pushes the rest of the scope into a synthesized
// `else`. Both leave NO `func_return` node, so the existing if-scope machinery
// handles early termination — no runner flag. `return` is only valid inside a
// function body, and the rewrite is per-scope, so a top-level guard (the common
// idiom, e.g. adv_multi_out_07) is handled exactly; a `return` nested in a loop
// or several `if` levels deep falls back to the prior behavior.
std::string_view Prp2lnast::scan_overflow_in_gap(uint32_t prev_end, uint32_t gap_end) const {
  using namespace std::string_view_literals;
  if (prev_end >= gap_end) {
    return {};
  }
  // The `wrap`/`sat` overflow keyword is an anonymous prefix the prpparse CST
  // does not materialize, so recover it from the raw source gap before the
  // statement. The gap can also hold the enclosing scope's opening `{` (first
  // statement) or trailing `//`/`/* */` comments, so strip comments and take
  // the LAST identifier run rather than requiring the whole gap to equal it.
  std::string_view g = text_between(prev_end, gap_end);
  std::string      stripped;
  stripped.reserve(g.size());
  for (size_t k = 0; k < g.size();) {
    if (k + 1 < g.size() && g[k] == '/' && g[k + 1] == '/') {
      while (k < g.size() && g[k] != '\n') {
        ++k;  // line comment to EOL
      }
    } else if (k + 1 < g.size() && g[k] == '/' && g[k + 1] == '*') {
      k = skip_nested_block_comment(g, k);  // comments nest
      stripped.push_back(' ');              // a comment separates tokens
    } else {
      stripped.push_back(g[k]);
      ++k;
    }
  }
  size_t end = stripped.size();
  while (end > 0
         && (stripped[end - 1] == ' ' || stripped[end - 1] == '\t' || stripped[end - 1] == '\r' || stripped[end - 1] == '\n')) {
    --end;
  }
  size_t begin = end;
  while (begin > 0
         && ((stripped[begin - 1] >= 'a' && stripped[begin - 1] <= 'z')
             || (stripped[begin - 1] >= 'A' && stripped[begin - 1] <= 'Z'))) {
    --begin;
  }
  std::string_view last(stripped.data() + begin, end - begin);
  if (last == "wrap"sv) {
    return "wrap"sv;
  }
  if (last == "sat"sv) {
    return "sat"sv;
  }
  return {};
}

void Prp2lnast::lower_children_range(TSNode parent, uint32_t from) {
  using namespace std::string_view_literals;
  const uint32_t   nc = ts_node_child_count(parent);
  std::string_view pending_overflow;
  uint32_t         prev_end = (from == 0) ? ts_node_start_byte(parent) : ts_node_end_byte(ts_node_child(parent, from - 1));
  for (uint32_t i = from; i < nc; i++) {
    TSNode           c = ts_node_child(parent, i);
    std::string_view t(ts_node_type(c));

    if (!ts_node_is_named(c)) {
      if (t == "wrap"sv || t == "sat"sv) {
        pending_overflow = t;
      }
      continue;
    }
    // A scope_statement already consumed as a lambda body must not re-walk.
    if (consumed_lambda_body_starts.contains(ts_node_start_byte(c))) {
      pending_overflow = {};
      prev_end         = ts_node_end_byte(c);
      continue;
    }

    // Scope attributes (`{ ::[abc=…, color=…] … }`, 2opt-freq B) are consumed
    // by process_scope_statement — never a statement. Skip by TYPE so every
    // walk over this child list (including the guarded-return re-entries at
    // lower_children_range(parent, i+1)) stays clean.
    if (t == "attribute_sq"sv) {
      prev_end = ts_node_end_byte(c);
      continue;
    }

    // Early-return rewrite (see header comment). Only fires when a `return` is
    // actually present, so return-free scopes lower exactly as before.
    if (ts_is_bare_return(c)) {
      // In a return-flag function (one with a return inside a loop), a `return`
      // sets the flag (and `break`s the enclosing loop, if any) so the loop
      // unrolls stop and the post-loop continuation guards skip the rest.
      if (!return_flag_name_.empty()) {
        Pending_src pending_guard(*lnast, mint_src(c));
        auto        sidx = builder.add_child(Lnast_ntype::create_store());
        lnast->add_child(sidx, Lnast_node::create_ref(return_flag_name_));
        lnast->add_child(sidx, Lnast_node::create_const("true"));
        if (in_return_loop_) {
          auto bidx = builder.add_child(Lnast_ntype::create_func_break());
          lnast->add_child(bidx, builder.mint_tmp_ref());
        }
      }
      return;  // terminator: the rest of this scope is unreachable
    }
    TSNode gcond{}, gthen{};
    if (is_guarded_return_if(c, gcond, gthen)) {
      Pending_src pending_guard(*lnast, mint_src(c));
      Lnast_node  cref   = ts_node_is_null(gcond) ? Lnast_node::create_const("true") : expr_to_node(gcond);
      auto        if_idx = builder.add_child(Lnast_ntype::create_if());
      attach_loc(if_idx, c);
      lnast->add_child(if_idx, cref);
      auto then_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
      builder.push_stmts(then_idx);
      lower_children_range(gthen, 0);  // then-body; the tail `return` is handled recursively
      builder.pop_stmts();
      auto else_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
      builder.push_stmts(else_idx);
      lower_children_range(parent, i + 1);  // continuation: rest of this scope
      builder.pop_stmts();
      return;  // continuation absorbed into the else
    }
    // Return-flag mode (function has a return inside a loop): a compound
    // statement (`if`/loop, not the clean guard above) that transitively
    // contains a `return` must guard its continuation on the flag — `if flag {
    // [break] } else { rest }` — so the rest of this scope is skipped once a
    // return fired (and an enclosing loop is broken). Loops also lower their
    // body with in_return_loop_ set, turning their inner returns into flag+break.
    if (!return_flag_name_.empty()) {
      const bool is_loop = (t == "for_statement"sv || t == "while_statement"sv || t == "loop_statement"sv);
      const bool is_if   = (t == "if_expression"sv);
      if ((is_loop || is_if) && ts_subtree_has_return(c)) {
        Pending_src pending_guard(*lnast, mint_src(c));
        const bool  saved_in_loop = in_return_loop_;
        if (is_loop) {
          in_return_loop_ = true;
        }
        process_statement(c);
        in_return_loop_ = saved_in_loop;
        // Continuation guard.
        auto if_idx     = builder.add_child(Lnast_ntype::create_if());
        attach_loc(if_idx, c);
        lnast->add_child(if_idx, Lnast_node::create_ref(return_flag_name_));
        auto then_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
        if (in_return_loop_) {  // nested in an enclosing loop → propagate the break outward
          builder.push_stmts(then_idx);
          auto bidx = builder.add_child(Lnast_ntype::create_func_break());
          lnast->add_child(bidx, builder.mint_tmp_ref());
          builder.pop_stmts();
        }
        auto else_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
        builder.push_stmts(else_idx);
        lower_children_range(parent, i + 1);
        builder.pop_stmts();
        return;  // continuation absorbed into the else
      }
    }

    const bool is_assign = (t == "assignment"sv);
    if (is_assign && pending_overflow.empty()) {
      pending_overflow = scan_overflow_in_gap(prev_end, ts_node_start_byte(c));
    }
    if (is_assign && !pending_overflow.empty()) {
      pending_overflow_kind = pending_overflow;
    }
    process_statement(c);
    pending_overflow_kind = {};
    pending_overflow      = {};
    prev_end              = ts_node_end_byte(c);
  }
}

void Prp2lnast::process_statement(TSNode n) {
  if (ts_node_is_null(n)) {
    return;
  }
  // Statement-granularity provenance: every def-bearing LNAST node
  // created while this statement lowers — op nodes, SSA temps, synthesized
  // stores — inherits the statement's span id. Finer anchors (attach_loc at
  // ~30 sites) override per node; nested statements re-enter here and narrow.
  Pending_src pending_guard(*lnast, mint_src(n));

  using Handler                                                             = void (Prp2lnast::*)(TSNode);
  static const absl::flat_hash_map<std::string_view, Handler> stmt_dispatch = {
      {"declaration_statement", &Prp2lnast::process_declaration_statement},
      {           "assignment",            &Prp2lnast::process_assignment},
      {      "while_statement",       &Prp2lnast::process_while_statement},
      {        "for_statement",         &Prp2lnast::process_for_statement},
      {       "loop_statement",        &Prp2lnast::process_loop_statement},
      {       "tick_statement",        &Prp2lnast::process_tick_statement},
      {       "step_statement",        &Prp2lnast::process_step_statement},
      {    "control_statement",     &Prp2lnast::process_control_statement},
      {               "lambda",      &Prp2lnast::process_lambda_statement},
      {      "enum_assignment",       &Prp2lnast::process_enum_assignment},
      {       "type_statement",        &Prp2lnast::process_type_statement},
      {     "import_statement",      &Prp2lnast::process_import_statement},
      {       "test_statement",        &Prp2lnast::process_test_statement},
      {      "spawn_statement",       &Prp2lnast::process_spawn_statement},
      {       "impl_statement",        &Prp2lnast::process_impl_statement},
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
      "paren_group",
      "attribute_set",
      "constant",
  };

  std::string_view t(ts_node_type(n));
  if (t == "comment") {
    return;
  }
  if (t == "formal_statement") {
    // 2f-verify `formal name.dotted { ... }` block: a declarative verification
    // overlay consumed ONLY by `lhd formal` (which re-parses the file and
    // evaluates the body against the design's encoded signals). It never
    // lowers to hardware — the design compile skips it entirely, like a
    // comment (same file can carry both design code and formal blocks).
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
  // if/match used as a statement: the value is discarded, so skip the
  // result tmp + per-arm placeholder `assign ___N = 0` that the expression
  // form emits.
  if (t == "if_expression") {
    (void)if_expr_to_node(n, /*need_result=*/false);
    return;
  }
  if (t == "match_expression") {
    (void)match_expr_to_node(n, /*need_result=*/false);
    return;
  }
  // `cassert(...)` / `assert(...)` are plain function calls in the new
  // grammar (the dedicated `assert_statement` rule is gone). Detect them at
  // statement position and emit a `cassert` LNAST node so the verifier
  // counts the assertion. Other function calls fall through to the generic
  // expr-as-statement path.
  if (t == "function_call_expression") {
    TSNode func = child_by_field(n, "function");
    if (!ts_node_is_null(func)) {
      auto fname = trim(get_text(func));
      // `requires`/`ensures` were REMOVED from the language: they never
      // generated an obligation, and a silent-no-op
      // contract is worse than no contract. They are now ordinary undefined
      // calls — write `assume` for a precondition, `assert` for a
      // postcondition.
      if (fname == "cassert" || fname == "assert" || fname == "assume" || fname == "assume_nocheck" || fname == "assert_always") {
        TSNode arg_tuple = child_by_field(n, "argument");
        if (!ts_node_is_null(arg_tuple)) {
          // The argument tuple is `(cond)` or `(cond, "msg")`. Lower the
          // first arg as the condition and the optional second arg as a
          // diagnostic message: a `cassert` node becomes
          //   cassert(<cond>)            — no message
          //   cassert(<cond>, <msg>)     — message read by the verifier when
          //                                the assertion is statically false
          // (see uPass_verifier::classify_statement). The message is any
          // expression that resolves to a comptime string (a string literal or
          // an interpolated string), lowered the same way as the condition.
          uint32_t   nnc             = ts_node_named_child_count(arg_tuple);
          TSNode     cond_node       = nnc >= 1 ? ts_node_named_child(arg_tuple, 0) : TSNode{};
          const bool saved_in_assert = in_assert_lowering_;
          in_assert_lowering_        = true;  // `.[bw_max]`/`.[bw_min]` legal here
          Lnast_node cond_ref        = ts_node_is_null(cond_node) ? Lnast_node::create_const("true") : expr_to_node(cond_node);
          // Lower the message (if any) BEFORE creating the cassert node so the
          // helper statements an interpolated string emits land ahead of the
          // assertion in source order (otherwise the message ref would dangle).
          bool       have_msg        = nnc >= 2;
          Lnast_node msg_ref;
          if (have_msg) {
            msg_ref = expr_to_node(ts_node_named_child(arg_tuple, 1));
          }
          in_assert_lowering_ = saved_in_assert;
          auto idx            = builder.add_child(Lnast_ntype::create_cassert());
          attach_loc(idx, n);  // source span → verifier can point at this assertion
          lnast->add_child(idx, cond_ref);
          // The cassert NODE name does not survive upass re-emission, but its
          // CHILDREN do — so carry the obligation kind (assume / assert_always /
          // cassert) as a sentinel const child ahead of the optional user
          // message. A plain `assert` adds no sentinel (tolg then defaults the
          // kind to assert). `cassert` MUST carry one: it is an ELABORATION
          // check, not a design obligation — it never reaches pass.formal and
          // never becomes a runtime check, so tolg has to tell it from `assert`.
          if (fname == "assume" || fname == "assume_nocheck" || fname == "assert_always" || fname == "cassert") {
            // UNQUOTED on purpose. A user message is always a STRING const
            // (`'…'`), so an unquoted spelling can never collide with one. The
            // old quoted form was matched downstream with an unanchored
            // substring search, which let a message merely CONTAINING
            // "__fkind__assume" retype an `assert` into an `assume` — the
            // solver then used it as a hypothesis and reported a genuinely
            // false property as PROVEN, at exit 0.
            lnast->add_child(idx, Lnast_node::create_const(std::string("__fkind__") + std::string{fname}));
          }
          if (have_msg) {
            lnast->add_child(idx, msg_ref);
          }
          return;
        }
      }
    }
  }
  if (expr_stmt.contains(t)) {
    // A pure expression at statement position computes a value that is never
    // used (e.g. `a + 1`, `a == b`, a bare `a`, `(x, y)`). Warn — unless the
    // expression has an observable side effect somewhere inside it (a function
    // call, an assignment, an `::[attr=…]` write, …), in which case the
    // statement is run for that effect and the discarded value is not useless.
    // Value-producing trailing expressions (a code-block / if / match value, a
    // single-expression comb output) never reach here — they lower through
    // expr_to_node directly, not process_statement.
    if (!expr_has_side_effects(n)) {
      report_warning(n,
                     "unused-expression",
                     "syntax",
                     "expression result is unused (statement has no effect)",
                     "remove the statement, or assign / use its value");
    }
    (void)expr_to_node(n);
    return;
  }
  // A statement kind with no dispatch entry is SILENTLY DROPPED from the LNAST —
  // the rest of the pipeline then reasons about a program that is missing code.
  // This used to be a bare `std::print`, so a dropped statement did not count as
  // an error OR a warning: `tick_statement` and `step_statement` fell through
  // here, the whole body of every `test` block's cycle loop vanished, and a
  // testbench `assert` was folded against a variable's stale initializer while
  // the run still reported "0 errors, 0 warnings" (lhdsuite fixme issue 2).
  //
  // It is a WARNING rather than an error because it reports a front-end gap, not
  // a user mistake, and the partial tree is often still usable. But it must be
  // visible: any future statement kind added to the grammar without a handler
  // here now announces itself instead of quietly deleting code.
  report_warning(n,
                 "unhandled-statement",
                 "internal",
                 std::format("statement kind `{}` has no LNAST lowering and was dropped", t),
                 "add a handler to Prp2lnast::stmt_dispatch — the statement is missing from the compiled program");
}

void Prp2lnast::process_scope_statement(TSNode n, Lnast_nid /*target_stmts*/) {
  // Every body lowered through here is nested under a conditional / loop /
  // match arm / lambda (or an inline scope expression) — never the top-level
  // file scope (process_description walks the root directly). Bump
  // conditional_depth_ so a write inside cannot capture/update a file-scope
  // comptime int binding (see Binding::int_value). Its declarations close
  // with it (capture_frames_).
  Conditional_scope   guard(&conditional_depth_);
  Capture_frame_guard frame(capture_frames_, Capture_frame{});

  TSNode attrs = child_by_field(n, "attributes");
  if (ts_node_is_null(attrs)) {
    walk_statement_block(n);
    return;
  }

  bool legacy_scope = false;
  for (TSNode item : ts_node_named_children(attrs)) {
    auto lv = child_by_field(item, "lvalue");
    if (!ts_node_is_null(lv)) {
      auto k        = trim(get_text(lv));
      legacy_scope |= k != "synth" && !k.starts_with("synth.");
    }
  }
  if (!legacy_scope) {
    auto idx = builder.add_child(Lnast_ntype::create_stmts());
    attach_loc(idx, n);
    builder.push_stmts(idx);
    emit_synth_scope(attrs, 2);
    walk_statement_block(n);
    builder.pop_stmts();
    return;
  }
  // 2opt-freq B: `{ ::[abc="…", color=…] stmts }` — the annotated block is its
  // own synthesis partition region. Lower to a NESTED stmts (tolg recurses
  // plain stmts transparently) whose first statement is one compiler-minted
  // marker
  //     attr_set(%__region_<id>, "__region", <abc-string | true>)
  // tolg stamps every LGraph node generated inside the stmts with node color
  // <id> and records the abc string as coloring_info "region_opts"[<id>]
  // (consumed by pass.abc, which maps each color region separately). The
  // runner DCE keeps "__region" markers alive (dce_is_keepalive_attr_set);
  // the `%` target namespace skips scope/shadowing checks by construction.
  int                                         region_id = 0;
  TSNode                                      abc_rv{};
  std::vector<std::pair<std::string, TSNode>> options;
  if (!parse_scope_attributes(attrs, region_id, abc_rv, options)) {
    return;  // diag already emitted
  }
  auto sidx = builder.add_child(Lnast_ntype::create_stmts());
  attach_loc(sidx, n);
  builder.push_stmts(sidx);
  {
    Pending_src pending_guard(*lnast, mint_src(attrs));
    auto        idx = builder.add_child(Lnast_ntype::create_attr_set());
    attach_loc(idx, attrs);
    // The region id is the LEADING integer of the target suffix; the trailing
    // per-block counter keeps each marker's target unique (two same-label
    // blocks carry different payloads, and the attribute store is add-only
    // per name — one write per target name, ever).
    lnast->add_child(idx, Lnast_node::create_ref(std::format("%__region_{}_{}", region_id, region_marker_seq_++)));
    lnast->add_child(idx, Lnast_node::create_const("__region"));
    if (!ts_node_is_null(abc_rv)) {
      lnast->add_child(idx, expr_to_node(abc_rv));
    } else {
      lnast->add_child(idx, Lnast_node::create_const("true"));
    }
  }
  for (const auto& [key, value] : options) {
    Pending_src pending_guard(*lnast, mint_src(value));
    auto        idx = builder.add_child(Lnast_ntype::create_attr_set());
    attach_loc(idx, value);
    lnast->add_child(idx, Lnast_node::create_ref(std::format("%__region_{}_{}", region_id, region_marker_seq_++)));
    lnast->add_child(idx, Lnast_node::create_const("__region_" + key));
    lnast->add_child(idx, expr_to_node(value));
  }
  emit_synth_scope(attrs, 2);
  walk_statement_block(n);
  builder.pop_stmts();
}

// Parse `::[abc="…", color=<int|"label">]` on a scope block (strict, unlike
// the open declaration-attribute vocabulary: a mistyped synthesis hint must
// never silently no-op). `color` picks the region id — an integer literal is
// used as-is (two blocks with the same id become the same region group), a
// string label is interned per file (same label => same region), and with no
// `color` a fresh id is auto-allocated. Returns false after a diag.
bool Prp2lnast::parse_scope_attributes(TSNode attr_list_node, int& region_id, TSNode& abc_rv,
                                       std::vector<std::pair<std::string, TSNode>>& options) {
  bool have_color = false;
  for (TSNode item : ts_node_named_children(attr_list_node)) {
    std::string_view it(ts_node_type(item));
    if (it != "attribute_assignment") {
      report_error(item,
                   "scope-attr-shape",
                   "syntax",
                   "scope attributes must be key=value pairs",
                   "use { ::[abc=\"<abc flow>\", color=<int or \"label\">] ... }");
      return false;
    }
    TSNode lv = child_by_field(item, "lvalue");
    TSNode rv = child_by_field(item, "rvalue");
    if (ts_node_is_null(lv) || ts_node_is_null(rv)) {
      report_error(item, "scope-attr-shape", "syntax", "scope attribute needs an explicit value", "abc=\"…\" or color=…");
      return false;
    }
    // Attribute values arrive as `constant` nodes; classify by text. A quoted
    // value is a string. abc= flows contain `{D}` (ABC's delay substitution),
    // which a double-quoted Pyrope string would interpolate — require single
    // quotes when a `{` is present so the flow stays verbatim.
    auto value_txt = [&](TSNode v) { return trim(get_text(v)); };
    auto is_quoted = [](std::string_view t) {
      return t.size() >= 2 && ((t.front() == '\'' && t.back() == '\'') || (t.front() == '"' && t.back() == '"'));
    };
    auto key = trim(get_text(lv));
    if (key == "synth" || key.starts_with("synth.")) {
      continue;
    }
    if (key == "abc") {
      auto txt = value_txt(rv);
      if (!is_quoted(txt) || (txt.front() == '"' && txt.find('{') != std::string_view::npos)) {
        report_error(rv,
                     "scope-attr-value",
                     "syntax",
                     "abc= takes a plain string literal (the per-region ABC flow/options)",
                     "single-quote it so `{D}` stays verbatim: abc='strash; resyn2; &get -n; &nf {D}; &put'");
        return false;
      }
      abc_rv = rv;
    } else if (key == "ware" || key == "delay") {
      const auto txt = value_txt(rv);
      if (key == "ware") {
        if (txt != "true" && txt != "false") {
          report_error(rv, "scope-attr-value", "syntax", "ware= takes true or false", "e.g. ware=true");
          return false;
        }
      } else {
        uint32_t ps          = 0;
        const auto [ptr, ec] = std::from_chars(txt.data(), txt.data() + txt.size(), ps);
        if (ec != std::errc{} || ptr != txt.data() + txt.size()) {
          report_error(rv,
                       "scope-attr-value",
                       "syntax",
                       "delay= takes a non-negative integer number of picoseconds",
                       "delay=0 selects area; e.g. delay=500 selects timing");
          return false;
        }
      }
      if (std::any_of(options.begin(), options.end(), [&](const auto& o) { return o.first == key; })) {
        report_error(lv, "scope-attr-value", "syntax", "duplicate synthesis scope option", "specify each option once");
        return false;
      }
      options.emplace_back(std::string(key), rv);
    } else if (key == "color") {
      have_color = true;
      auto txt   = value_txt(rv);
      if (is_quoted(txt)) {
        // strip the quotes; same label anywhere in this file => same region
        txt = txt.substr(1, txt.size() - 2);
        if (txt.empty()) {
          report_error(rv, "scope-attr-value", "syntax", "color= label must not be empty", "e.g. color=\"crit\"");
          return false;
        }
        auto [itr, inserted] = region_label_ids_.try_emplace(std::string{txt}, 0);
        if (inserted) {
          itr->second = alloc_region_id();
        }
        region_id = itr->second;
      } else {
        int         v = 0;
        const auto* b = txt.data();
        const auto* e = txt.data() + txt.size();
        auto [p, ec]  = std::from_chars(b, e, v);
        if (ec != std::errc{} || p != e || v <= 0) {
          report_error(rv,
                       "scope-attr-value",
                       "syntax",
                       "color= takes a positive integer region id or a string label",
                       "e.g. color=2 or color=\"crit\"");
          return false;
        }
        region_id = v;
        region_ids_used_.insert(v);
      }
    } else {
      report_error(lv,
                   "scope-attr-unknown",
                   "syntax",
                   std::format("unknown scope attribute '{}'", key),
                   "scope blocks accept abc=\"…\", color=…, ware=true|false, and delay=<ps>");
      return false;
    }
  }
  if (!have_color) {
    region_id = alloc_region_id();
  }
  return true;
}

int Prp2lnast::alloc_region_id() {
  while (region_ids_used_.contains(next_region_id_)) {
    ++next_region_id_;
  }
  region_ids_used_.insert(next_region_id_);
  return next_region_id_;
}

// Resolved modifiers of a `var_or_let_or_reg` decl node.
struct Decl_mods {
  std::string_view kind;  // storage kind ("mut"/"const"/"reg"/"stage"/…; empty = none)
  bool             has_comptime{false};
  bool             has_pub{false};  // `pub` export modifier
};

// ---------------- Declaration / Assignment ----------------

// Map a `var_or_let_or_reg` storage child's node kind to the storage-class
// keyword used downstream (attr_set "type" const|mut|reg|stage).
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
  if (t == "wire_decl") {
    // 2c-wire — single-driver combinational net; like `mut` its value is built
    // from if/match, but reads are position-independent (see Mode::wire_kind).
    return "wire";
  }
  if (t == "stage_decl") {
    // `stage[N] lhs = rhs` (mod-only): lhs is rhs delivered N
    // cycles later. The decl-merge converts the kind to a `reg` declare
    // carrying a trailing stages(min,max) node (the upass/pipe shape).
    return "stage";
  }
  return {};
}

// Resolve (storage-kind, has-comptime, has-pub) for a `var_or_let_or_reg` decl
// node.
static Decl_mods decode_decl(TSNode decl) {
  if (ts_node_is_null(decl)) {
    return {};
  }
  TSNode           storage = ts_node_child_by_field_name(decl, "storage", 7);
  TSNode           cpt     = ts_node_child_by_field_name(decl, "comptime", 8);
  TSNode           pub     = ts_node_child_by_field_name(decl, "pub", 3);
  std::string_view kind    = ts_node_is_null(storage) ? std::string_view{} : storage_kind_from_node_type(ts_node_type(storage));
  return {kind, !ts_node_is_null(cpt), !ts_node_is_null(pub)};
}

std::vector<int64_t> Prp2lnast::extract_array_dims(TSNode type_cast_node) const {
  std::vector<int64_t> out;
  if (ts_node_is_null(type_cast_node)) {
    return out;
  }
  TSNode ty = ts_node_child_by_field_name(type_cast_node, "type", 4);
  if (ts_node_is_null(ty) || std::string_view(ts_node_type(ty)) != "array_type") {
    return out;
  }
  while (!ts_node_is_null(ty) && std::string_view(ts_node_type(ty)) == "array_type") {
    TSNode len = ts_node_child_by_field_name(ty, "length", 6);
    if (ts_node_is_null(len)) {
      return {};
    }
    TSNode                 idx = ts_node_child_by_field_name(len, "index", 5);
    // A `constant` literal OR a compile-time-resolvable `const`/`mut` binding
    // (`[sz]u8` where `sz` was statically known at the declaration point) — both
    // resolve through resolve_cycle_value (which reads Binding::int_value,
    // tracking decl-time-known muts with the same UPDATE/ERASE rules). The
    // resolved length must still be > 0, as before.
    std::optional<int64_t> v   = resolve_cycle_value(idx);
    if (!v || *v <= 0) {
      return {};
    }
    out.push_back(*v);
    ty = ts_node_child_by_field_name(ty, "base", 4);
  }
  return out;
}

// Validate a `pub` value declaration (the LiveHD docs):
// only file-scope `const` (optionally `comptime`) declarations are
// exportable. `pub mut`/`pub reg` contradict the read-only export semantics.
void Prp2lnast::check_pub_value_decl(TSNode decl_node, std::string_view kind) {
  if (!builder.at_top_stmts() || !lambda_kind_stack_.empty() || conditional_depth_ > 0) {
    report_error(decl_node,
                 "pub-not-file-scope",
                 "syntax",
                 "`pub` is only valid on file-scope declarations",
                 "move the declaration to the file's top level");
  }
  if (kind != "const") {
    // `pub reg`/`pub stage` is a likely-common mistake: registers ARE shared
    // across modules, just not via `pub` (a read-only value/lambda export).
    // Point at the dedicated `regref` mechanism rather than the generic
    // "use pub const" hint. The grammar still parses `pub reg`; this
    // prp2lnast check is the single enforcement point (no grammar change).
    if (kind == "reg" || kind == "stage") {
      report_error(decl_node,
                   "pub-reg-use-regref",
                   "type",
                   std::format("cannot export `pub {}`: a register cannot be a `pub` export", kind),
                   "to share a register across modules use `regref`, not `pub reg`");
    }
    report_error(decl_node,
                 "pub-requires-const",
                 "type",
                 std::format("cannot export `pub {}`: exported state must be read-only", kind.empty() ? "fluid" : kind),
                 "use `pub const` for values, or export a `comb`/`mod`/`pipe`/`fluid` definition");
  }
}

void Prp2lnast::process_declaration_statement(TSNode n) {
  // declaration_statement: var_or_let_or_reg + lvalue (typed_identifier or list)
  auto decl_node   = child_by_field(n, "decl");
  auto lvalue_node = child_by_field(n, "lvalue");
  if (ts_node_is_null(lvalue_node)) {
    return;
  }

  // Decode storage class + comptime/pub modifiers from the decl's structured fields.
  auto [kind, has_comptime, has_pub] = decode_decl(decl_node);

  // A timed stage decl with no initializer (`stage[1] out@[4]`) is LEGAL (qa.md Q39 item 7,
  // docs 15-tbd.md "Other parser rules"): the value arrives from a later assignment.

  // `pub` marks a file-scope const value declaration exportable.
  if (has_pub) {
    check_pub_value_decl(decl_node, kind);
  }

  // A `comb` may not declare a `reg` — same ruling as the assignment form
  // (process_lvalue_for_assign), which used to be the ONLY site that checked.
  // Keying the check on the initializer left a hole: an UNINITIALIZED
  // `reg r:u8` (this path — a declaration_statement has no rvalue) sailed
  // through and only surfaced far downstream as tolg's baffling "instance of
  // clocked 'c' but 'top' has no clock to forward (needs_clock bug)". A latch
  // (`reg l:u8:[latch=true]`) is spelled `reg`, so it is covered here too
  // (2f-latch M0; the six latch fixtures moved to `pub mod` for this reason).
  if (kind == "reg" && !lambda_kind_stack_.empty() && lambda_kind_stack_.back() == "comb") {
    report_error(decl_node,
                 "reg-in-comb",
                 "type",
                 "a `comb` may not declare a `reg` (combinational logic holds no state)",
                 "move the register into a `pipe`/`mod` body, or pass the value through an output");
  }

  // For each typed_identifier in the lvalue, emit: attr_set <ref> "type" kind
  auto emit_decl_attrs = [&](TSNode ti) {
    TSNode id = child_by_field(ti, "identifier");
    if (ts_node_is_null(id)) {
      return;
    }
    if (has_pub) {
      // Canonical spelling, as the lambda pub site does: uPass_constprop looks
      // the export up by the declaration's canonical name.
      lnast->add_pub(canonical_escaped_ident(trim(get_text(id))), "value", mint_src(id));
    }
    // 2f-type_bound — emit any non-foldable integer type bound's statements
    // FIRST. They must precede the `declare` that consumes them, and
    // rewrite_decls_to_declare merges a CONTIGUOUS attr_set/type_spec run, so
    // nothing may be inserted once the cluster head below is emitted.
    prelower_type_bounds(child_by_field(ti, "type"));
    Lnast_node ref = identifier_to_node(id, /*for_lvalue=*/true);
    check_capture_shadow(ref.get_name(), id);
    auto& b      = note_binding(ref.get_name(), has_comptime ? Bind_kind::comptime : Bind_kind::runtime);
    b.fold_const = !has_comptime && kind == "const";
    b.replayable = kind == "const" || kind == "mut";
    {
      auto idx = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("type"));
      lnast->add_child(idx, Lnast_node::create_const(kind));
      // Span → the decl-merge copies this onto the synthesized `declare` so
      // declaration-site diagnostics (semacheck redeclaration/shadowing) can
      // point at the `mut/const x` line. Mirrors the assignment-form site.
      attach_loc(idx, id);
      // `stage[N] out@[4]` (no initializer, legal): the (min,max) still rides
      // the cluster head as the trailing stages node, exactly as in the
      // assignment form, so the decl-merge builds a depth-N stage `declare`
      // and the later `out = rhs` store creates the pipeline flop.
      if (kind == "stage") {
        if (lambda_kind_stack_.empty() || lambda_kind_stack_.back() != "mod") {
          report_error(decl_node,
                       "stage-not-in-mod",
                       "type",
                       "`stage[N]` is only valid inside `mod` bodies",
                       lambda_kind_stack_.empty() || lambda_kind_stack_.back() == "comb"
                           ? "comb logic is all at cycle 0 — use a plain assignment"
                           : "pipe bodies use stage inference — use an explicit `reg` stage register instead");
        }
        TSNode storage          = ts_node_child_by_field_name(decl_node, "storage", 7);
        auto [min_txt, max_txt] = parse_stage_slot(storage);
        auto st                 = lnast->add_child(idx, Lnast_ntype::create_stages());
        lnast->add_child(st, Lnast_node::create_const(min_txt));
        lnast->add_child(st, Lnast_node::create_const(max_txt));
      }
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
    if (kind == "stage") {
      // `stage[1] out@[4]`: keep the landing-cycle check, as the assignment form does.
      TSNode timing = child_by_field(ti, "timing");
      if (ts_node_is_null(timing) && !ts_node_is_null(tc)) {
        timing = child_by_field(tc, "timing");
      }
      maybe_emit_timecheck(timing, id);
    }
  };

  std::string_view lvtype(ts_node_type(lvalue_node));
  if (lvtype == "typed_identifier") {
    emit_decl_attrs(lvalue_node);
  } else if (lvtype == "typed_identifier_list") {
    for (TSNode c : ts_node_named_children(lvalue_node)) {
      emit_decl_attrs(c);
    }
  }
}

Lnast_node Prp2lnast::process_lvalue_for_assign(TSNode lvalue, const Lnast_node& rvalue, TSNode decl_node, TSNode type_cast_node,
                                                bool rhs_is_fcall, std::string_view rhs_fcall_name, std::string_view overflow_kind,
                                                bool rhs_name_bindable, std::optional<int64_t> resolved_rvalue_int,
                                                bool rvalue_comptime) {
  std::string_view lvt(ts_node_type(lvalue));
  if (lvt == "lvalue_list") {
    // Tuple lvalue: `(x0, x1, …) = rhs`. Each item is an `lvalue_item`,
    // which wraps either a `typed_identifier`, a `(_complex_identifier,
    // optional type_cast)`, or a `named_lvalue` (rename form). When the
    // item exposes a plain identifier name (typed_identifier or bare
    // identifier), we bind BY NAME — `tuple_get` keyed by the local
    // name, not by position. This matches the named-arg call rule:
    // `(c, b) = dox(a)` picks field `c` and field `b` from the return
    // bundle regardless of declaration order in the function's signature.
    // For complex lvalues (member_selection, bit_selection, etc.) we
    // fall back to positional binding since there's no name to use.
    uint32_t pos = 0;
    for (TSNode item : ts_node_named_children(lvalue)) {
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

      std::string_view inner_t(ts_node_type(inner));

      // Rename form: `(x = dox.b, y = dox.c) = dox(...)`. The `name` field
      // is the local identifier to bind, the `lvalue` field is either a
      // bare identifier (whole return tuple → name) or a `dot_expression`
      // whose leading identifier identifies the source (callee name for a
      // single fcall RHS, or positional slot for a tuple-of-fcalls RHS) —
      // the trailing path picks the deep field.
      if (inner_t == "named_lvalue") {
        TSNode name_node = child_by_field(inner, "name");
        TSNode src_node  = child_by_field(inner, "lvalue");
        if (!ts_node_is_null(name_node) && !ts_node_is_null(src_node)) {
          std::vector<std::string>    path_keys;
          std::string                 prefix;
          std::function<void(TSNode)> walk = [&](TSNode n) {
            std::string_view t(ts_node_type(n));
            if (t == "dot_expression") {
              TSNode dot_item = child_by_field(n, "item");
              if (ts_node_is_null(dot_item)) {
                bool first = true;  // first named child is the base — recurse, rest are keys
                for (TSNode c : ts_node_named_children(n)) {
                  if (first) {
                    first = false;
                    walk(c);
                    continue;
                  }
                  path_keys.emplace_back(trim(get_text(c)));
                }
              } else {
                walk(dot_item);
                for (TSNode c : ts_node_named_children(n)) {
                  if (ts_node_eq(c, dot_item)) {
                    continue;
                  }
                  path_keys.emplace_back(trim(get_text(c)));
                }
              }
            } else if (t == "typed_identifier") {
              TSNode id = child_by_field(n, "identifier");
              if (!ts_node_is_null(id)) {
                prefix = std::string(trim(get_text(id)));
              }
            } else if (t == "identifier") {
              prefix = std::string(trim(get_text(n)));
            }
          };
          walk(src_node);
          if (rhs_is_fcall && prefix != rhs_fcall_name) {
            // 2i-import (A8/5b): a single call on the RHS is unambiguous, so a
            // bare output name needs no callee prefix — `(r = io_result) = f(…)`
            // binds r to f's `io_result` output. Reinterpret the lone identifier
            // as a field pick on the single call's return. A dotted source that
            // still names a different callee (`g.b`) stays a hard error (genuine
            // cross-function reuse).
            if (path_keys.empty()) {
              path_keys.emplace_back(std::move(prefix));
            } else {
              report_error(inner,
                           "destructure-prefix",
                           "name",
                           std::format("destructure rename prefix `{}` does not match call `{}` in `{}`",
                                       prefix,
                                       rhs_fcall_name,
                                       get_text(inner)));
            }
          }
          // Emit a chain of single-key tuple_gets — constprop chains tmp
          // through each step, while a single multi-key tuple_get against a
          // fcall return wasn't propagating through nested tuples. For
          // tuple-of-fcalls RHS, prepend a positional tuple_get on the
          // current lvalue_list slot so the prefix's path applies to the
          // matching call's return.
          if (destructure_rhs_ == Destructure_rhs::unnamed_var) {
            // qa.md Appendix 8: an UNNAMED tuple has no names to match, and a
            // rename slot must not silently bind slot 0.
            report_error(inner,
                         "destructure-rename-unnamed",
                         "name",
                         std::format("destructure slot `{}`: the right-hand side is an unnamed tuple, it has no field names",
                                     get_text(inner)),
                         "bind an unnamed tuple by position with bare names, `(a, b) = t`");
          }
          if (!rhs_is_fcall && destructure_rhs_ == Destructure_rhs::rooted && !prefix.empty()) {
            // A named variable on the right: the slot's source path starts at
            // that tuple's own fields (`(x=a, y=b.c) = t`).
            path_keys.insert(path_keys.begin(), std::move(prefix));
          }
          Lnast_node tmp = rvalue;
          if (!rhs_is_fcall && destructure_rhs_ == Destructure_rhs::legacy) {
            auto       tg_idx = builder.add_child(Lnast_ntype::create_tuple_get());
            Lnast_node next   = builder.mint_tmp_ref();
            lnast->add_child(tg_idx, next);
            lnast->add_child(tg_idx, tmp);
            lnast->add_child(tg_idx, Lnast_node::create_const(std::to_string(pos)));
            tmp = next;
          }
          for (const auto& k : path_keys) {
            auto       tg_idx = builder.add_child(Lnast_ntype::create_tuple_get());
            Lnast_node next   = builder.mint_tmp_ref();
            lnast->add_child(tg_idx, next);
            lnast->add_child(tg_idx, tmp);
            lnast->add_child(tg_idx, Lnast_node::create_const(k));
            tmp = next;
          }
          (void)process_lvalue_for_assign(name_node,
                                          tmp,
                                          decl_node,
                                          item_tc,
                                          false,
                                          {},
                                          overflow_kind,
                                          false,
                                          std::nullopt,
                                          rvalue_comptime);
          ++pos;
          continue;
        }
      }

      // Decide between name-driven and positional tuple_get. When the RHS
      // is a function call, the destructure binds by return-field name —
      // so we key tuple_get by the local's identifier text. For tuple-
      // literal RHS (e.g. `(a, b) = (b+1, a)`) positional binding is
      // correct because the literal's slots are positional anyway.
      std::string key_text;
      bool        use_name_key = false;
      // Bind by NAME when the RHS is a function call OR any name-bindable RHS
      // (a named-tuple literal / a variable bound to a named tuple). A named
      // tuple is stored field-by-name (the bundle map is lexically sorted, so
      // declaration order is NOT preserved) — it must be read by field name,
      // not position (03-bundle.md "Named-tuple destructuring"). An UNNAMED
      // positional literal RHS keeps positional binding (rhs_name_bindable
      // false), so `(a,b) = (2,3)` / tuple-swap still work.
      if (rhs_is_fcall || rhs_name_bindable) {
        if (inner_t == "identifier") {
          key_text     = std::string(trim(get_text(inner)));
          use_name_key = true;
        } else if (inner_t == "typed_identifier") {
          TSNode id = child_by_field(inner, "identifier");
          if (!ts_node_is_null(id)) {
            key_text     = std::string(trim(get_text(id)));
            use_name_key = true;
          }
        }
      }

      // tuple_get tmp, rvalue, <key>
      auto       tg_idx = builder.add_child(Lnast_ntype::create_tuple_get());
      Lnast_node tmp    = builder.mint_tmp_ref();
      lnast->add_child(tg_idx, tmp);
      lnast->add_child(tg_idx, rvalue);
      if (use_name_key) {
        lnast->add_child(tg_idx, Lnast_node::create_const(key_text));
      } else {
        lnast->add_child(tg_idx, Lnast_node::create_const(std::to_string(pos)));
      }
      // Recurse: the per-position tmp is the new "rvalue" for the inner lvalue.
      // `decl_node` propagates so `mut (a, b) = …` declares both items.
      (void)
          process_lvalue_for_assign(inner, tmp, decl_node, item_tc, false, {}, overflow_kind, false, std::nullopt, rvalue_comptime);
      ++pos;
    }
    return rvalue;
  }
  if (lvt == "timed_identifier") {
    // `out@[4] = rhs` (with or without a stage decl): the lvalue is
    // the inner identifier; `@[4]` is a pure landing-cycle check (never a
    // flop), recorded as an inert timecheck statement.
    TSNode inner = child_by_field(lvalue, "identifier");
    if (!ts_node_is_null(inner)) {
      maybe_emit_timecheck(child_by_field(lvalue, "timing"), inner);
      return process_lvalue_for_assign(inner,
                                       rvalue,
                                       decl_node,
                                       type_cast_node,
                                       rhs_is_fcall,
                                       rhs_fcall_name,
                                       overflow_kind,
                                       rhs_name_bindable,
                                       resolved_rvalue_int,
                                       rvalue_comptime);
    }
  }
  if (lvt == "identifier" || lvt == "typed_identifier") {
    TSNode id = (lvt == "typed_identifier") ? child_by_field(lvalue, "identifier") : lvalue;
    TSNode tc = (lvt == "typed_identifier") ? child_by_field(lvalue, "type") : type_cast_node;
    if (ts_node_is_null(id)) {
      id = lvalue;
    }
    // `x:u8@[2] = rhs` body form: the timing rides the
    // typed_identifier (or its type_cast) and is a pure check, same as the
    // timed_identifier wrapper above.
    if (lvt == "typed_identifier") {
      TSNode timing = child_by_field(lvalue, "timing");
      if (ts_node_is_null(timing) && !ts_node_is_null(tc)) {
        timing = child_by_field(tc, "timing");
      }
      maybe_emit_timecheck(timing, id);
    }
    // Determine if this introduces a new declaration (decl_node present)
    bool             has_decl = !ts_node_is_null(decl_node);
    std::string_view kind_sv;
    bool             has_cpt = false;
    bool             has_pub = false;
    if (has_decl) {
      auto pr = decode_decl(decl_node);
      kind_sv = pr.kind;
      has_cpt = pr.has_comptime;
      has_pub = pr.has_pub;
    }

    // `pub const x = …` marks the value exportable (validated:
    // file-scope, const storage). Destructuring (`pub const (a,b) = t`)
    // records each item — decl_node propagates through the recursion.
    if (has_pub) {
      check_pub_value_decl(decl_node, kind_sv);
      lnast->add_pub(canonical_escaped_ident(trim(get_text(id))), "value", mint_src(id));
    }

    // The statically known integer of this write (see Binding::int_value), so
    // a later `@[NAME]`, `stage[NAME]`, `pipe[NAME]` timing slot or integer
    // type bound can resolve the name:
    //
    //   - `const NAME = <compile-time integer expression>` → record
    //       (immutable, always valid, so no depth gate).
    //   - `mut NAME = <compile-time integer expression>` → record with
    //       declaration-time-capture. A later statement-level plain write of
    //       another resolvable expression UPDATES it, and ANY other write
    //       ERASEs it. The erase covers a runtime rhs, a compound op (the
    //       scalar `rvalue` arrives as a tmp ref, never a const, so is_const()
    //       is false), and any write nested in a conditional/loop body deeper
    //       than the declaration (which makes the value no longer statically
    //       known — conditional_depth_ != Binding::decl_depth). The rule is
    //       the same in every scope (user ruling 2026-09-28 (32)): a `mod` or
    //       `comb` body tracks its own `mut` exactly like the file scope.
    //
    // A timing slot then resolves the value known AT THE LAMBDA DECLARATION
    // POINT.
    std::optional<int64_t> known_int;
    {
      // A `wrap`/`sat` write narrows the value into the declared type, and a
      // bit-pattern literal (0sb/0ub) into a typed binding is a reinterpret:
      // neither is the rvalue's own integer. `mut t0:U1 = 0; wrap t0 = 0sb1011`
      // recorded -5 and `x#[t0]` failed "negative bit index" (random Pyrope
      // round-trip fuzz, 2026-10-08). Leave those statically unknown.
      const bool typed_write = !ts_node_is_null(tc) || (!has_decl && [&] {
        const auto* b = find_binding(canonical_escaped_ident(trim(get_text(id))));
        return b != nullptr && b->typed;
      }());
      const bool reinterpret = !overflow_kind.empty()
                               || (typed_write && rvalue.is_const() && rvalue.get_name().size() > 2 && rvalue.get_name()[0] == '0'
                                   && (rvalue.get_name()[1] == 's' || rvalue.get_name()[1] == 'u'));
      auto as_int = [&]() -> std::optional<int64_t> {
        if (reinterpret) {
          return std::nullopt;
        }
        if (resolved_rvalue_int) {
          return resolved_rvalue_int;
        }
        if (!rvalue.is_const()) {
          return std::nullopt;
        }
        if (auto cst = Dlop::from_pyrope(rvalue.get_name()); cst && cst->is_integer() && cst->is_just_i64()) {
          return cst->to_just_i64();
        }
        return std::nullopt;
      };
      if (has_decl && (kind_sv == "const" || kind_sv == "mut")) {
        known_int = as_int();  // a declaration is unconditional in its own scope
      } else if (!has_decl) {
        if (auto* b = find_binding(canonical_escaped_ident(trim(get_text(id))))) {
          b->int_value = conditional_depth_ == b->decl_depth ? as_int() : std::nullopt;
        }
      }
    }

    // Nested-lambda capture scope (see capture_frames_): a `comptime const` is
    // a comptime binding, and so is a plain `const` whose value is a
    // compile-time constant (ruling 2026-09-28 #24: a known integer, an
    // expression over comptime bindings, a comptime entity such as an import
    // namespace, an enum type or a lambda) or exported `pub` (a pub value must
    // fold). Every other declaration is a runtime binding.
    if (has_decl) {
      check_capture_shadow(canonical_escaped_ident(trim(get_text(id))), id);
      const bool comptime_decl
          = has_cpt || (kind_sv == "const" && (has_pub || rvalue_comptime || known_int.has_value() || is_comptime_rvalue(rvalue)));
      auto& b = note_binding(canonical_escaped_ident(trim(get_text(id))), comptime_decl ? Bind_kind::comptime : Bind_kind::runtime);
      b.int_value  = known_int;
      b.fold_const = !comptime_decl && kind_sv == "const";
      b.replayable = kind_sv == "const" || kind_sv == "mut";
      if (!ts_node_is_null(tc)) {
        b.typed = true;
        b.range = folded_int_type_range(child_by_field(tc, "type"));
      }
    }
    // A file-scope import binding is replayed into every streamed lambda. Keys
    // use the same canonical spelling as declarations and calls.
    if (conditional_depth_ == 0 && rvalue.is_ref()) {
      if (auto it = capture_import_bindings_.find(std::string(rvalue.get_name())); it != capture_import_bindings_.end()) {
        capture_import_bindings_.insert_or_assign(std::string(canonical_escaped_ident(trim(get_text(id)))),
                                                  std::string(it->second));
      }
    }

    // 2f-type_bound — same rule as the bare-declaration path in emit_decl_attrs:
    // a non-foldable integer type bound lowers to statements that must precede
    // the contiguous attr_set/type_spec cluster below. This is the path a
    // declaration WITH an initializer takes (`reg d:unsigned(bits=N) = nil`).
    if (has_decl) {
      prelower_type_bounds(tc);
    }
    Lnast_node ref = identifier_to_node(id, /*for_lvalue=*/true);
    Lnast_nid  reg_decl_head{};  // set for `reg` decls; the init value rides this cluster head
    if (has_decl) {
      auto idx = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("type"));
      lnast->add_child(idx, Lnast_node::create_const(kind_sv));
      // Span → the decl-merge copies this onto the synthesized `declare` so
      // declaration-site diagnostics can point at the `mut/const x` line.
      attach_loc(idx, id);
      // A stage decl rides its (min,max) as a trailing stages node
      // on the cluster head; the decl-merge moves it onto the synthesized
      // declare (mode "reg") so tolg lowers it as a depth-N pipeline Flop.
      if (kind_sv == "stage") {
        if (lambda_kind_stack_.empty() || lambda_kind_stack_.back() != "mod") {
          report_error(decl_node,
                       "stage-not-in-mod",
                       "type",
                       "`stage[N]` is only valid inside `mod` bodies",
                       lambda_kind_stack_.empty() || lambda_kind_stack_.back() == "comb"
                           ? "comb logic is all at cycle 0 — use a plain assignment"
                           : "pipe bodies use stage inference — use an explicit `reg` stage register instead");
        }
        TSNode storage          = ts_node_child_by_field_name(decl_node, "storage", 7);
        auto [min_txt, max_txt] = parse_stage_slot(storage);
        auto st                 = lnast->add_child(idx, Lnast_ntype::create_stages());
        lnast->add_child(st, Lnast_node::create_const(min_txt));
        lnast->add_child(st, Lnast_node::create_const(max_txt));
      }
      // A `comb` may not declare a `reg` (06-functions.md: comb
      // logic is all at cycle 0 and holds no state; the `::[debug]` exception
      // is out of scope). pipe/mod bodies and file scope may.
      if (kind_sv == "reg") {
        if (!lambda_kind_stack_.empty() && lambda_kind_stack_.back() == "comb") {
          report_error(decl_node,
                       "reg-in-comb",
                       "type",
                       "a `comb` may not declare a `reg` (combinational logic holds no state)",
                       "move the register into a `pipe`/`mod` body, or pass the value through an output");
        }
        reg_decl_head = idx;
      }
    }
    if (has_cpt) {
      auto idx = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("comptime"));
      lnast->add_child(idx, Lnast_node::create_const("true"));
    }
    if (!ts_node_is_null(tc)) {
      // A type cast on the lvalue is only legal when it (a) declares the
      // variable (`var c:u4 = …`, decl_node present) or (b) is the cast
      // target of a `wrap`/`sat` write (`wrap c:u4 = …`). A bare
      // `c:u4 = …` re-assignment would otherwise emit a stmts-level
      // `type_spec` that silently re-types an existing variable — reject it
      // and point the user at wrap/sat for an intentional downcast.
      if (!has_decl && overflow_kind.empty()) {
        std::string name(trim(get_text(id)));
        report_error(tc,
                     "assign-retype",
                     "type",
                     std::format("cannot change the type of `{}` in an assignment", name),
                     std::format("annotate the type at the declaration, or use `wrap`/`sat` to downcast "
                                 "(e.g. `wrap {}{} = …`)",
                                 name,
                                 std::string_view(get_text(tc))));
      }
      // A literal initializer must match the declared scalar kind (same check as
      // a function parameter `b:bool = 3` — see emit_arg_assign / check_decl_init_kind).
      check_decl_init_kind(trim(get_text(id)), rvalue, child_by_field(tc, "type"), tc);
      emit_type_spec(ref, tc);
    }

    // An inline tuple type already installed its default field values in
    // emit_type_spec. A declaration's nil initializer supplies no overrides;
    // emitting another whole-value nil store would erase those defaults.
    if (has_decl && reg_decl_head.is_invalid() && overflow_kind.empty() && !ts_node_is_null(tc) && rvalue.is_const()
        && rvalue.get_name() == "nil") {
      const auto ty = child_by_field(tc, "type");
      if (!ts_node_is_null(ty) && std::string_view(ts_node_type(ty)) == "expression_type") {
        for (TSNode child : ts_node_named_children(ty)) {
          if (std::string_view(ts_node_type(child)) == "tuple") {
            return ref;
          }
        }
      }
    }

    // Emit assign. A `wrap`/`sat` write lowers the value through a
    // `wrap|sat(v=<value>, type=<lhs>)` library call first; the call result
    // then binds the lvalue. attributes narrows the value to the lhs's declared
    // type (read via the `type=` arg), bitwidth exempts the lhs from the
    // overflow check, and codegen emits get_mask / mux+get_mask. The type_spec
    // above runs first so the lhs's type is known when the call is processed.
    Lnast_node store_value = rvalue;
    // `mut x:[] = nil` (UNSIZED array type + nil init) declares an EMPTY array,
    // not a nil scalar — so a later self-splice append (`x = (...x, e)`) can
    // grow it. Seed it with an empty tuple (which establishes a tuple shape, so
    // typecheck's "nil scalar cannot re-shape to a tuple" guard never fires)
    // instead of a nil scalar. Sized arrays (`:[N]T`) keep their prefill path;
    // reg arrays are left to the declare cluster. (2f-splice: array_nil_shape.)
    if (has_decl && reg_decl_head.is_invalid() && !ts_node_is_null(tc) && rvalue.is_const() && rvalue.get_name() == "nil") {
      TSNode ty = child_by_field(tc, "type");
      if (!ts_node_is_null(ty) && std::string_view(ts_node_type(ty)) == "array_type" && extract_array_dims(tc).empty()) {
        auto ta_idx = builder.add_child(Lnast_ntype::create_tuple_add());
        auto empty  = builder.mint_tmp_ref();
        lnast->add_child(ta_idx, empty);  // empty tuple_add — zero entries
        store_value = empty;
      }
    }
    // Documented implicit conversion at a typed scalar declaration:
    //   `mut b:int = a`  ==  `mut b = int(a)`   (a:string)
    //   `mut c:string = b` == `mut c = string(b)`           (02-basics.md)
    // Insert the cast so a cross-kind initializer converts instead of tripping
    // the typecheck assign-type-mismatch. ONLY for an UNBOUNDED `int` / `string`
    // declared type: those have no overflow envelope, so an idempotent int()/
    // string() wrap is harmless. A SIZED/RANGED int (`u3`, `s4`, `int(min,max)`)
    // is excluded — wrapping its initializer in `int(...)` would defeat the
    // bitwidth overflow fit-check (the cast result is fit-exempt) and mangle a
    // bit-pattern literal's unknown bits. `bool` stays explicit (boolean(...));
    // nil/array/reg/wrap-sat are also excluded.
    if (has_decl && reg_decl_head.is_invalid() && overflow_kind.empty() && !ts_node_is_null(tc)) {
      const TSNode      ty          = child_by_field(tc, "type");
      const Scalar_kind dk          = declared_scalar_kind(ty);
      // An UNBOUNDED `string` declaration normalizes its literal initializer with
      // an idempotent `string(...)` wrap. (The old unbounded-`int` wrap is gone:
      // there is no value-preserving integer cast anymore — `signed`/`unsigned`
      // are sign reinterprets that need a fully-typed input — so a bare
      // `:signed`/`:unsigned`/`:uint` integer initializer is bound directly.)
      const bool        is_nil_init = store_value.is_const() && store_value.get_name() == "nil";
      if (dk == Scalar_kind::string && extract_array_dims(tc).empty() && !is_nil_init) {
        Lnast_node casted = builder.mint_tmp_ref();
        auto       fc     = builder.add_child(Lnast_ntype::create_func_call());
        lnast->add_child(fc, casted);
        lnast->add_child(fc, Lnast_node::create_ref("String"));
        lnast->add_child(fc, store_value);
        store_value = casted;
      }
    }
    if (!overflow_kind.empty()) {
      Lnast_node wrapped = builder.mint_tmp_ref();
      auto       fc      = builder.add_child(Lnast_ntype::create_func_call());
      lnast->add_child(fc, wrapped);                                // dst tmp
      lnast->add_child(fc, Lnast_node::create_ref(overflow_kind));  // callee: wrap | sat
      auto va = lnast->add_child(fc, Lnast_ntype::create_store());  // v = <value>
      lnast->add_child(va, Lnast_node::create_ref("v"));
      lnast->add_child(va, rvalue);
      auto ta = lnast->add_child(fc, Lnast_ntype::create_store());  // type = <lhs> (its declared type)
      lnast->add_child(ta, Lnast_node::create_ref("type"));
      lnast->add_child(ta, ref);
      store_value = wrapped;
    }
    if (!reg_decl_head.is_invalid()) {
      // Declare-folding — a `reg` declaration's initializer is its
      // power-on/reset value, NOT a din write: ride it on the decl cluster
      // head so the decl-merge folds it into the declare's optional 4th
      // [value] child. Only genuine re-assignments emit a `store` (every
      // store to a reg-declared name is a next-state write).
      lnast->add_child(reg_decl_head, store_value);
    } else {
      auto aidx = builder.add_child(Lnast_ntype::create_store());
      lnast->add_child(aidx, ref);
      lnast->add_child(aidx, store_value);
      // Span → bitwidth's "does not fit" overflow diagnostic can point at the
      // write site (the declare/store loc-carry chain: here, prp_copy_one_node,
      // and the runner's emit_push).
      attach_loc(aidx, id);
    }

    // Phase 8 typesystem: a bare `true`/`false` literal on the rvalue
    // implies a `:bool` type. Inject a synthetic type_spec so the
    // attribute upass records the boolean envelope without requiring
    // the user to write `:bool` explicitly. Only fires when no explicit
    // type cast was given (it would dominate).
    if (ts_node_is_null(tc) && rvalue.is_const()) {
      auto rv_text = rvalue.get_name();
      if (rv_text == "true" || rv_text == "false") {
        auto ts_idx = builder.add_child(Lnast_ntype::create_type_spec());
        lnast->add_child(ts_idx, ref);
        lnast->add_child(ts_idx, Lnast_ntype::create_prim_type_bool());
      }
    }

    // Comptime vector/matrix pre-fill: `mut data:[N][M]T = scalar` populates
    // every flat slot so later element reads (`data[i][j]`) fold even though
    // each `tuple_set data i j …` would otherwise erase the bare scalar
    // trivial that `data = scalar` first stored. NOT for a `reg` array
    // (1a-mem): its scalar init rides the declare's [value] child (power-on
    // contents on the Memory `initial` pin); pre-fill stores would lower as
    // unconditional every-cycle write ports, and reg element reads are
    // runtime q reads that never fold regardless.
    if (has_decl && reg_decl_head.is_invalid() && !ts_node_is_null(tc) && rvalue.is_const()) {
      std::vector<int64_t> dims = extract_array_dims(tc);
      if (!dims.empty()) {
        std::string_view rv_text = rvalue.get_name();
        if (rv_text != "nil") {
          std::vector<int64_t> idx_v(dims.size(), 0);
          while (true) {
            auto ts_idx = builder.add_child(Lnast_ntype::create_store());
            lnast->add_child(ts_idx, ref);
            for (auto d : idx_v) {
              lnast->add_child(ts_idx, Lnast_node::create_const(std::to_string(d)));
            }
            lnast->add_child(ts_idx, Lnast_node::create_const(rv_text));
            int k = static_cast<int>(idx_v.size()) - 1;
            while (k >= 0) {
              if (++idx_v[k] < dims[k]) {
                break;
              }
              idx_v[k] = 0;
              --k;
            }
            if (k < 0) {
              break;
            }
          }
        }
      }
    }
    return ref;
  }
  if (lvt == "bit_selection") {
    // `b#[range] = rhs` lowers to a read-modify-write:
    //   - `set_mask new_word base mask rhs` synthesizes the updated word
    //     (pure-functional; matches LGraph Dlop set_mask_op shape).
    //   - The write-back is delegated to a recursive process_lvalue_for_assign
    //     on the argument lvalue, so nested forms like `a.b#[range] = rhs`
    //     reuse the member_selection arm and lower to a proper `tuple_set`.
    // No syntax for declaring/typing through a bit-range — reject decl/type
    // alongside the member_selection arm below.
    if (!ts_node_is_null(decl_node)) {
      report_error(lvalue,
                   "bit-range-decl",
                   "name",
                   std::format("cannot declare a bit-range lvalue: `{}`", get_text(lvalue)),
                   "declare the base variable first, then write the bit range");
    }
    if (!ts_node_is_null(type_cast_node)) {
      report_error(lvalue,
                   "bit-range-type",
                   "type",
                   std::format("cannot type-annotate a bit-range lvalue: `{}`", get_text(lvalue)),
                   "the base carries the type; annotate it at the base's declaration");
    }

    TSNode arg_n = child_by_field(lvalue, "argument");
    if (ts_node_is_null(arg_n)) {
      arg_n = ts_node_named_child(lvalue, 0);
    }
    TSNode sel_node{};
    for (uint32_t i = 0; i < child_count(lvalue); i++) {
      TSNode           c = child(lvalue, i);
      std::string_view ct(ts_node_type(c));
      if (ct == "select") {
        sel_node = c;
        break;
      }
    }

    // Read the argument's current value via the expression path. For a plain
    // identifier this is just a ref; for `a.b` this emits a tuple_get into a
    // tmp, which is exactly the old-value input we want feeding set_mask.
    Lnast_node cur_val    = expr_to_node(arg_n);
    int        lane_width = 0;
    const auto ranges     = bit_selection_ranges(sel_node, &lane_width);

    // A wrap/sat policy applies to the SELECTED lane, not to the whole base.
    // Give the policy call a synthetic unsigned type whose width is the number
    // of selected bits, then feed its narrowed result to set_mask.  A bare
    // write keeps the original rvalue so upass.bitwidth can reject a source
    // wider than the selected lane.  Only the lane WIDTH must be static (a
    // `acc#[(j*4)..+4]` window may move); a range whose width is not known at
    // compile time cannot define wrap/sat semantics here.
    Lnast_node store_value = rvalue;
    if (!overflow_kind.empty()) {
      // The lane type's max, 2^width - 1. A width counted by a compile-time
      // NAME (`comptime const n = 3; wrap a#[2..+n] = ..`, an attribute read, a
      // generic, a loop index) is a literal once folded here; when it only
      // folds after elaboration, the max rides the type_spec as a ref the
      // runner folds, like any deferred integer type bound (a loop whose body
      // bakes one unrolls, see plan_loop_roll; a lane that folds to no bits is
      // rejected by the runner's wrap/sat lowering).
      const Lnast_node one = Lnast_node::create_const("1");
      Lnast_node       lane_max;
      const auto       w = lane_width >= 1 ? std::nullopt : bit_range_lane_width(sel_node);
      if (lane_width >= 1) {
        lane_max = Lnast_node::create_const(std::string(Dlop::get_mask_value(lane_width)->to_pyrope()));
      } else if (w && !w->is_const()) {
        lane_max = emit_bound_binop(Lnast_ntype::create_minus(), emit_bound_binop(Lnast_ntype::create_shl(), one, *w), one);
      } else if (const auto& wv = w ? Dlop::from_pyrope_cached(w->get_name()) : Dlop{}; w && wv.is_positive()) {
        const auto n1 = Dlop::create_integer(1);
        lane_max      = Lnast_node::create_const(std::string(n1->shl_op(wv)->sub_op(*n1)->to_pyrope()));
      } else if (w) {
        // The literal forms get this from bit_selection_ranges; bounds that
        // are comptime NAMES fold only here.
        TSNode range_n = child_by_field(sel_node, "index");
        if (ts_node_is_null(range_n)) {
          range_n = child_by_field(sel_node, "range");
        }
        report_error(lvalue,
                     "descending-bit-range",
                     "type",
                     std::format("{} bit range `{}` is not allowed (ranges are increasing and select at least one bit)",
                                 wv.is_known_zero() ? "empty" : "descending",
                                 get_text(range_n)),
                     "write the bounds low-to-high, e.g. `x#[lo..=hi]` with lo <= hi");
        lane_max = Lnast_node::create_const("0");
      } else {
        report_error(lvalue,
                     "dynamic-bit-range-overflow-policy",
                     "type",
                     std::format("cannot apply `{}` to a bit-range whose width is not known at compile time", overflow_kind),
                     "use a constant-width destination range, or explicitly select the desired bits on the right-hand side");
        lane_max = Lnast_node::create_const("0");
      }

      Lnast_node type_ref = builder.mint_tmp_ref();
      auto       ts_idx   = builder.add_child(Lnast_ntype::create_type_spec());
      lnast->add_child(ts_idx, type_ref);
      auto pt = lnast->add_child(ts_idx, Lnast_ntype::create_prim_type_int());
      lnast->add_child(pt, lane_max);
      lnast->add_child(pt, Lnast_node::create_const("0"));

      Lnast_node narrowed = builder.mint_tmp_ref();
      auto       fc       = builder.add_child(Lnast_ntype::create_func_call());
      attach_loc(fc, lvalue);  // a lane width that folds late is diagnosed at this call
      lnast->add_child(fc, narrowed);
      lnast->add_child(fc, Lnast_node::create_ref(overflow_kind));
      auto va = lnast->add_child(fc, Lnast_ntype::create_store());
      lnast->add_child(va, Lnast_node::create_ref("v"));
      lnast->add_child(va, rvalue);
      auto ta = lnast->add_child(fc, Lnast_ntype::create_store());
      lnast->add_child(ta, Lnast_node::create_ref("type"));
      lnast->add_child(ta, type_ref);
      store_value = narrowed;
    }

    Lnast_node new_word      = cur_val;
    int        packed_offset = 0;
    for (const auto& [lo, hi] : ranges) {
      auto part = store_value;
      if (ranges.size() > 1) {
        auto get = builder.add_child(Lnast_ntype::create_get_mask());
        part     = builder.mint_tmp_ref();
        lnast->add_child(get, part);
        lnast->add_child(get, store_value);
        lnast->add_child(get, Lnast_node::create_const(packed_offset));
        lnast->add_child(get, Lnast_node::create_const(packed_offset + 1));
        ++packed_offset;
      }
      auto       sm   = builder.add_child(Lnast_ntype::create_set_mask());
      const auto next = builder.mint_tmp_ref();
      lnast->add_child(sm, next);
      lnast->add_child(sm, new_word);
      lnast->add_child(sm, part);
      lnast->add_child(sm, lo);
      lnast->add_child(sm, hi);
      attach_loc(sm, lvalue);
      new_word = next;
    }

    // Write the updated word back to the argument lvalue. Recursing keeps the
    // member_selection / dot_expression handling in one place; passing null
    // decl/type because they were already rejected (and don't apply to the
    // inner lvalue either — it's a re-bind, not a declaration).
    return process_lvalue_for_assign(arg_n, new_word, TSNode{}, TSNode{}, false, {});
  }
  if (lvt == "member_selection" || lvt == "dot_expression") {
    // Path-rooted lvalues can't carry a declaration or type cast: Pyrope has
    // no syntax for "declare new field through a path". Reject so we don't
    // silently drop the decl/type and emit a misleading `tuple_set`.
    if (!ts_node_is_null(decl_node)) {
      report_error(lvalue,
                   "tuple-path-decl",
                   "name",
                   std::format("cannot declare a tuple-path lvalue: `{}`", get_text(lvalue)),
                   "declare the base tuple instead");
    }
    if (!ts_node_is_null(type_cast_node)) {
      report_error(lvalue,
                   "tuple-path-type",
                   "type",
                   std::format("cannot type-annotate a tuple-path lvalue: `{}`", get_text(lvalue)),
                   "annotate the field at the base's declaration");
    }
    // Extract a deep path so `t.b[0] = …` lowers to `tuple_set t b 0 …`
    // (one tuple_set rooted on the actual variable) instead of going
    // through `tuple_get tmp t b; tuple_set tmp 0 …` — the tmp would not
    // propagate the update back to t. The walker handles arbitrarily
    // nested member_selection / dot_expression chains.
    std::vector<Lnast_node>     path;
    Lnast_node                  root;
    bool                        have_root = false;
    bool                        indexed   = false;  // a `[i]` selection on the path
    std::function<void(TSNode)> collect   = [&](TSNode n) {
      std::string_view t(ts_node_type(n));
      if (t == "dot_expression") {
        TSNode item = child_by_field(n, "item");
        if (ts_node_is_null(item)) {
          bool first = true;  // first named child is the base — recurse, rest are keys
          for (TSNode c : ts_node_named_children(n)) {
            if (first) {
              first = false;
              collect(c);
              continue;
            }
            std::string_view ct(ts_node_type(c));
            if (ct == "identifier") {
              path.push_back(Lnast_node::create_const(canonical_escaped_ident(trim(get_text(c)))));
            } else {
              path.push_back(expr_to_node(c));
            }
          }
        } else {
          collect(item);
          for (TSNode c : ts_node_named_children(n)) {
            if (ts_node_eq(c, item)) {
              continue;
            }
            std::string_view ct(ts_node_type(c));
            if (ct == "identifier") {
              path.push_back(Lnast_node::create_const(canonical_escaped_ident(trim(get_text(c)))));
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
        for (TSNode c : ts_node_named_children(n)) {
          if (!ts_node_is_null(arg) && ts_node_eq(c, arg)) {
            continue;
          }
          // Each `select` carries a single index expression, or an
          // open/from-zero range. Only the index form is supported as an
          // lvalue path today.
          TSNode idx_node = child_by_field(c, "index");
          if (!ts_node_is_null(idx_node)) {
            path.push_back(expr_to_node(idx_node));
            indexed = true;
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
    if (!overflow_kind.empty() && indexed) {
      // 04b-attributes.md: the prefix attaches to a plain name or a dotted
      // field; an entry picked with an index has no declared name to narrow to.
      report_error(lvalue,
                   "overflow-indexed-entry",
                   "type",
                   std::format("`{}` cannot apply to an entry picked with an index: `{}`", overflow_kind, trim(get_text(lvalue))),
                   std::format("narrow into a typed variable first (`{} v = …`, `v` of the entry's type) and store `v` into "
                               "the entry, or bit-select the value",
                               overflow_kind));
    }
    if (have_root) {
      Lnast_node store_val = rvalue;
      // A `wrap`/`sat` write to a FIELD (`wrap self.v += x`) must lower the value
      // through `wrap|sat(v=<value>, type=<field>)` just like a plain-variable
      // write (above). The `type` operand carries the field's declared envelope,
      // recovered by reading the field (a tuple_get on the same path). Without
      // this the qualifier was silently dropped and the field never wrapped.
      if (!overflow_kind.empty()) {
        // `type=` must be the lvalue's NAME, not a READ of it. Every consumer
        // resolves this operand through upass::decl_facts::lookup, which splits
        // "<root>.<field>" and reads the field's DECLARED envelope: attributes'
        // narrow_for_lhs, the runner's try_lower_wrap_sat, and bitwidth's
        // wrap-sat exemption. A tuple_get tmp only carries that envelope when the
        // field's value happens to be comptime-known (it rides the value copy in
        // Symbol_table::set), so for a `reg`/runtime field the width was unknown
        // and try_lower_wrap_sat declined -- leaving the `wrap(...)` call to reach
        // tolg, which reported it as "call to undefined function 'wrap'".
        //
        // Only a STATIC path can be named. A runtime index (`t[i].f`) has no
        // spellable declared name, so it keeps the tuple_get read.
        Lnast_node  type_ref;
        bool        static_path = root.is_ref() && !Lnast::is_tmp(root.get_name());
        std::string dotted(root.get_name());
        for (auto& p : path) {
          if (!p.is_const()) {
            static_path = false;
            break;
          }
          dotted += '.';
          dotted += p.get_name();
        }
        if (static_path) {
          type_ref = Lnast_node::create_ref(dotted);
        } else {
          type_ref = builder.mint_tmp_ref();
          auto tg  = builder.add_child(Lnast_ntype::create_tuple_get());
          lnast->add_child(tg, type_ref);
          lnast->add_child(tg, root);
          for (auto& p : path) {
            lnast->add_child(tg, p);
          }
        }
        Lnast_node wrapped = builder.mint_tmp_ref();
        auto       fc      = builder.add_child(Lnast_ntype::create_func_call());
        lnast->add_child(fc, wrapped);
        lnast->add_child(fc, Lnast_node::create_ref(overflow_kind));  // callee: wrap | sat
        auto va = lnast->add_child(fc, Lnast_ntype::create_store());  // v = <value>
        lnast->add_child(va, Lnast_node::create_ref("v"));
        lnast->add_child(va, rvalue);
        auto ta = lnast->add_child(fc, Lnast_ntype::create_store());  // type = <field> (its declared envelope)
        lnast->add_child(ta, Lnast_node::create_ref("type"));
        lnast->add_child(ta, type_ref);
        store_val = wrapped;
      }
      auto idx = builder.add_child(Lnast_ntype::create_store());
      lnast->add_child(idx, root);
      for (auto& p : path) {
        lnast->add_child(idx, p);
      }
      lnast->add_child(idx, store_val);
      return root;
    }
  }
  // Fallback: treat as a single assign using the text span.
  auto       name = trim(get_text(lvalue));
  Lnast_node ref  = Lnast_node::create_ref(name);
  auto       aidx = builder.add_child(Lnast_ntype::create_store());
  lnast->add_child(aidx, ref);
  lnast->add_child(aidx, rvalue);
  return ref;
}

// First call (anywhere under `n`) to a function declared with a `ref` parameter,
// else a null node. The callee method name is the last identifier of the call's
// `function` expression (a bare identifier, or a `dot_expression`/`member_selection`
// for UFCS). See ref_param_funcs_ / process_assignment.
TSNode Prp2lnast::find_ref_param_call(TSNode n) const {
  if (std::string_view(ts_node_type(n)) == "function_call_expression") {
    if (TSNode fnode = child_by_field(n, "function"); !ts_node_is_null(fnode)) {
      std::string_view ft(ts_node_type(fnode));
      std::string      nm;
      if (ft == "identifier") {
        nm = trim(get_text(fnode));
      } else if (ft == "dot_expression" || ft == "member_selection") {
        for (int i = static_cast<int>(ts_node_named_child_count(fnode)) - 1; i >= 0; --i) {
          TSNode c = ts_node_named_child(fnode, static_cast<uint32_t>(i));
          if (std::string_view(ts_node_type(c)) == "identifier") {
            nm = trim(get_text(c));
            break;
          }
        }
      }
      if (!nm.empty() && ref_param_funcs_.contains(nm)) {
        return n;
      }
    }
  }
  for (TSNode c : ts_node_named_children(n)) {
    if (TSNode r = find_ref_param_call(c); !ts_node_is_null(r)) {
      return r;
    }
  }
  return TSNode{};
}

void Prp2lnast::process_assignment(TSNode n) {
  TSNode decl = child_by_field(n, "decl");
  TSNode lv   = child_by_field(n, "lvalue");
  TSNode op   = child_by_field(n, "operator");
  TSNode rv   = child_by_field(n, "rvalue");
  TSNode tc   = child_by_field(n, "type");  // outer type_cast on complex lvalue

  // A `ref`-parameter method/comb (`comb set_x(ref self, …)`) mutates its
  // receiver, so its RESULT cannot be consumed in a right-hand-side expression
  // (`mut a3 = a1.set_x(...)`). Only the in-place statement form `a1.set_x(...)`
  // is allowed (the receiver IS the result). A `mod`/`pipe` whole-rhs bind stays
  // legal — those return a value without mutating the caller (2f-ufcs).
  if (!ts_node_is_null(rv) && !ref_param_funcs_.empty()) {
    if (TSNode bad = find_ref_param_call(rv); !ts_node_is_null(bad)) {
      report_error(bad,
                   "ref-method-in-rhs",
                   "type",
                   "a method with a `ref` parameter mutates its receiver and cannot be used in a right-hand-side "
                   "expression",
                   "call it as a statement (`obj.method(...)`) — the receiver is the result");
    }
  }

  // Keep the RHS node of a top-level `const NAME = "str" | (tuple)` so a later
  // `enum(...NAME, …)` spread can splice it in place (see const_rvalue_nodes_),
  // and of `const NAME = enum(…)` so its entries fold as a `mod`/`pipe` input
  // default (enum_entry_value).
  if (!ts_node_is_null(decl) && !ts_node_is_null(lv) && !ts_node_is_null(rv) && conditional_depth_ == 0
      && std::string_view(ts_node_type(lv)) == "identifier" && decode_decl(decl).kind == "const") {
    if (std::string_view rvt(ts_node_type(rv)); rvt == "constant" || rvt == "tuple" || rvt == "enum_definition") {
      // Clone the RHS subtree into the persistent arena (2f-stream): the spread
      // can be a LATER top-level statement, after this construct's arena is reset.
      prpparse::Ast* kept = clone_prp_subtree(retained_arena_, rv.a);
      prpparse::link_parents(kept);
      // Canonical spelling, like every other declaration key.
      const_rvalue_nodes_.insert_or_assign(canonical_escaped_ident(trim(get_text(lv))), TSNode{kept, prp_buf.get()});
    }
  }

  // Stable SSA/tmp ids: name every temp produced while lowering this statement
  // after its destination variable (`___<lhs>_<n>`) so editing or inserting an
  // unrelated statement does not renumber these temps. Covers the rvalue
  // expression and the lvalue write below; the guard restores the enclosing
  // scope on return (a nested lambda body keeps its own scope). See
  // Lnast_builder::set_tmp_scope.
  Lnast_builder::Tmp_scope_guard tmp_scope_guard(builder, ts_node_is_null(lv) ? std::string_view{} : get_text(lv));

  // ── Reject an unparenthesized multi-element tuple on either side ──────────
  //
  // A multi-target assignment requires explicit parentheses around the tuple:
  // `(a, b) = (2, 3)` is legal, but `mut a,b = (2,3)` and `(a,b) = 2,3` are not.
  // Without the parens the grammar can only consume the FIRST element as the
  // lvalue/rvalue and parks the remaining comma-separated element(s) in an
  // ERROR node that is a direct child of the `assignment` — which the rest of
  // this function would otherwise silently drop (e.g. lowering `mut a,b=(2,3)`
  // to just `a = (2,3)`, dropping `b`).
  //
  // The pyrope grammar also emits ERROR nodes for benign quirks (a `uint`/`sint`
  // type inside a `type_cast`, or the stray `:` in `const p::[attr] :u8`); those
  // carry NO comma, so a direct `,` child of the ERROR is the unambiguous
  // tell-tale of a dropped tuple slot. See prp2lnast's tree-sitter notes /
  // check_parse_errors (ERROR nodes are otherwise tolerated).
  {
    uint32_t nc = ts_node_child_count(n);
    for (uint32_t i = 0; i < nc; i++) {
      TSNode c = ts_node_child(n, i);
      if (std::string_view(ts_node_type(c)) != "ERROR") {
        continue;
      }
      bool     has_comma = false;
      uint32_t ec        = ts_node_child_count(c);
      for (uint32_t j = 0; j < ec; j++) {
        if (std::string_view(ts_node_type(ts_node_child(c, j))) == ",") {
          has_comma = true;
          break;
        }
      }
      if (!has_comma) {
        continue;  // benign grammar quirk (uint/sint type_cast, `::[attr] :T`), not a dropped tuple
      }
      // lhs vs rhs: the dropped element sits before the `=` for an
      // unparenthesized lhs tuple, after it for an unparenthesized rhs tuple.
      const bool before_op = ts_node_is_null(op) || ts_node_start_byte(c) < ts_node_start_byte(op);
      report_error(c,
                   "tuple-requires-parens",
                   "syntax",
                   std::format("a multi-element tuple on the {} of an assignment requires explicit parentheses",
                               before_op ? "left-hand-side" : "right-hand-side"),
                   "wrap the targets in parentheses, e.g. `(a, b) = (2, 3)`");
    }
  }

  // The assignment_operator wrapper has a single named child of the aliased
  // op kind (`assign`, `assign_add`, …). Drives the compound-op lowering, no
  // text compares.
  std::string_view op_kind;
  if (!ts_node_is_null(op)) {
    TSNode op_inner = ts_node_named_child(op, 0);
    op_kind         = ts_node_is_null(op_inner) ? std::string_view{} : std::string_view(ts_node_type(op_inner));
  } else {
    op_kind = "assign";  // assignment without an explicit operator defaults to plain `=`
  }

  // 2f-mutconst: a tuple-literal DECLARATION — an untyped `const`/`mut` binding —
  // must declare each NAMED field's kind (`const`/`mut`); a bare `b = 2` is a
  // compile error. The field kind is NOT required where it comes from context:
  // function-call args / comparison RHS (those never reach here as a `decl`) and
  // assignment to an already-TYPED variable (excluded by the no-type gate).
  // Positional entries and nested tuples are unaffected.
  if (!ts_node_is_null(decl) && ts_node_is_null(tc) && op_kind == "assign" && !ts_node_is_null(rv)
      && std::string_view(ts_node_type(rv)) == "tuple") {
    const auto dk       = decode_decl(decl).kind;
    const bool lv_typed = !ts_node_is_null(lv) && !ts_node_is_null(child_by_field(lv, "type"));
    if ((dk == "const" || dk == "mut") && !lv_typed) {
      for (TSNode f : ts_node_named_children(rv)) {
        std::string_view ft(ts_node_type(f));
        if (ft != "assignment" && ft != "simple_assignment") {
          continue;  // positional entry / comment — no name, no kind keyword needed
        }
        if (ts_node_is_null(child_by_field(f, "decl"))) {
          report_error(f,
                       "tuple-field-needs-kind",
                       "type",
                       "a named field in a tuple-literal declaration must declare its kind (`const` or `mut`)",
                       "write `const NAME = …` or `mut NAME = …` (bare fields are only allowed in calls, "
                       "comparisons, or a typed-variable assignment)");
        }
      }
    }
  }

  // The grammar attaches a statement-level `wrap`/`sat` prefix as the
  // `overflow` field of the enclosing _statement. Passed to
  // process_lvalue_for_assign, which lowers the scalar write through a
  // `wrap|sat(v=<value>, type=<lhs>)` library call. Consumed once
  // per assignment.
  const std::string_view overflow_kind = pending_overflow_kind;
  pending_overflow_kind                = {};

  // Resolve before lowering: an expression such as `W + LOG` becomes a tmp
  // ref in LNAST, which no longer exposes the value to the declaration's
  // compile-time binding capture below.
  std::optional<int64_t> resolved_rvalue_int;
  if (!ts_node_is_null(rv)) {
    if (auto v = resolve_type_int_value(rv); v && v->is_just_i64()) {
      resolved_rvalue_int = v->to_just_i64();
    }
  }
  // Same for a plain `const` initialized with any other compile-time constant
  // (`const w = N * 4` over a generic N): a comptime binding a nested lambda
  // may read (ruling 2026-09-28 #24).
  const bool rvalue_comptime
      = !ts_node_is_null(decl) && !ts_node_is_null(rv) && decode_decl(decl).kind == "const" && is_comptime_expression(rv);

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
        {         "assign_add",         [] { return Lnast_ntype::create_plus(); }},
        {         "assign_sub",        [] { return Lnast_ntype::create_minus(); }},
        {         "assign_mul",         [] { return Lnast_ntype::create_mult(); }},
        {         "assign_div",          [] { return Lnast_ntype::create_div(); }},
        {      "assign_bit_or",       [] { return Lnast_ntype::create_bit_or(); }},
        {     "assign_bit_and",      [] { return Lnast_ntype::create_bit_and(); }},
        {     "assign_bit_xor",      [] { return Lnast_ntype::create_bit_xor(); }},
        {         "assign_shl",          [] { return Lnast_ntype::create_shl(); }},
        {         "assign_sra",          [] { return Lnast_ntype::create_sra(); }},
        {"assign_tuple_concat", [] { return Lnast_ntype::create_tuple_concat(); }},
        {      "assign_log_or",       [] { return Lnast_ntype::create_log_or(); }},
        {     "assign_log_and",      [] { return Lnast_ntype::create_log_and(); }},
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
    (void)process_lvalue_for_assign(lv, result, decl, tc, false, {}, overflow_kind);
    return;
  }

  const bool  rhs_is_fcall = !ts_node_is_null(rv) && std::string_view(ts_node_type(rv)) == "function_call_expression";
  std::string rhs_fcall_name;
  if (rhs_is_fcall) {
    TSNode fn = child_by_field(rv, "function");
    if (!ts_node_is_null(fn)) {
      rhs_fcall_name = std::string(trim(get_text(fn)));
      // A call-site instance-name attribute (`Callee::[name=x]`) is part of the
      // function node's text, but the destructure field-extractions on the LHS
      // (`Callee.field`) name the BARE callee. Compare against that base name so
      // a named multi-output instantiation `(t = C.o, …) = C::[name=i](…)` is
      // accepted (the annotation only sets the Sub's hierarchical name).
      if (auto p = rhs_fcall_name.find("::["); p != std::string::npos) {
        rhs_fcall_name = std::string(trim(std::string_view(rhs_fcall_name).substr(0, p)));
      }
    }
  }
  // A destructure RHS is "name-bindable" unless it is an UNNAMED positional
  // tuple literal `(e0, e1, …)` — those bind by position; everything else (a
  // named-tuple literal, or a variable bound to a named tuple) binds by name.
  bool rhs_positional_literal = !ts_node_is_null(rv) && std::string_view(ts_node_type(rv)) == "tuple";
  if (rhs_positional_literal) {
    for (TSNode f : ts_node_named_children(rv)) {
      std::string_view ft(ts_node_type(f));
      if (ft == "assignment" || ft == "simple_assignment") {
        rhs_positional_literal = false;  // has a named field → name-bindable
        break;
      }
    }
  }
  // A variable bound to an UNNAMED tuple literal binds by position too:
  // named-vs-unnamed is a property of the value, not of how it is spelled.
  bool rhs_unnamed_var = false;
  if (!ts_node_is_null(rv) && !rhs_is_fcall && std::string_view(ts_node_type(rv)) == "identifier") {
    if (const auto* b = find_binding(canonical_escaped_ident(trim(get_text(rv)))); b != nullptr && b->unnamed_tuple) {
      rhs_unnamed_var = true;
    }
  }
  // `mut (c, d) = 1`: a destructuring list of 2+ slots needs a tuple on the
  // right; a single value is not broadcast (docs 03-bundle.md "mut (c,d) = 1
  // // error: 2 entry tuple in lhs, same in rhs").
  if (!ts_node_is_null(lv) && !ts_node_is_null(rv) && std::string_view(ts_node_type(lv)) == "lvalue_list"
      && std::string_view(ts_node_type(rv)) == "constant") {
    size_t slots = 0;
    for (TSNode item : ts_node_named_children(lv)) {
      slots += std::string_view(ts_node_type(item)) == "lvalue_item" ? 1 : 0;
    }
    if (slots >= 2) {
      report_error(lv,
                   "destructure-arity",
                   "type",
                   std::format("destructuring {} names needs a {}-entry tuple on the right, found a single value", slots, slots),
                   "a single value is not broadcast to every slot; write `(v0, v1, ...)` or assign each name");
    }
  }
  const auto saved_rhs = destructure_rhs_;
  destructure_rhs_
      = rhs_unnamed_var ? Destructure_rhs::unnamed_var
        : (rhs_is_fcall || rhs_positional_literal || ts_node_is_null(rv) || std::string_view(ts_node_type(rv)) == "tuple")
            ? Destructure_rhs::legacy
            : Destructure_rhs::rooted;
  (void)process_lvalue_for_assign(lv,
                                  rvalue_node,
                                  decl,
                                  tc,
                                  rhs_is_fcall,
                                  rhs_fcall_name,
                                  overflow_kind,
                                  /*rhs_name_bindable=*/!rhs_positional_literal && !rhs_unnamed_var,
                                  resolved_rvalue_int,
                                  rvalue_comptime);
  destructure_rhs_ = saved_rhs;
  // Record the shape of a variable this statement bound to a tuple literal.
  if (!ts_node_is_null(lv) && std::string_view(ts_node_type(lv)) == "identifier") {
    if (auto* b = find_binding(canonical_escaped_ident(trim(get_text(lv)))); b != nullptr) {
      bool unnamed = !ts_node_is_null(rv) && std::string_view(ts_node_type(rv)) == "tuple";
      if (unnamed) {
        for (TSNode f : ts_node_named_children(rv)) {
          const std::string_view ft(ts_node_type(f));
          if (ft == "assignment" || ft == "simple_assignment" || trim(get_text(f)).starts_with("...")) {
            unnamed = false;
            break;
          }
        }
      }
      b->unnamed_tuple = unnamed;
    }
  }
}

// ---------------- Control Flow ----------------

Lnast_node Prp2lnast::emit_always_true_ref() {
  // `1 == 1` — a recomputed always-true REF used as the condition for the
  // infinite-loop forms. A literal `const 'true'` condition makes the runner
  // skip the in-loop constprop dispatch of the body, so a body break-guard
  // never folds and the loop mis-terminates; a ref condition takes the proven
  // data-dependent unroll path. constprop folds the `1==1` away (no leak).
  auto idx = builder.add_child(Lnast_ntype::create_eq());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, Lnast_node::create_const("1"));
  lnast->add_child(idx, Lnast_node::create_const("1"));
  return ref;
}

void Prp2lnast::lower_infinite_loop(TSNode code, TSNode loc) {
  // `loop {}` / `while {}` / `while true {}` ≡
  //   while (1==1) { if (1==1) { body } else { break } }
  // Same shape as a data-dependent `while cond { if cond {body} else {break} }`,
  // but with a recomputed always-true ref so the runner folds the body each
  // iteration (the body must `break` to terminate). The `else { break }` is dead
  // for the infinite form (1==1 never false) and folds away.
  Lnast_node cond_ref  = emit_always_true_ref();
  auto       while_idx = builder.add_child(Lnast_ntype::create_while());
  attach_loc(while_idx, loc);
  lnast->add_child(while_idx, cond_ref);
  auto body_idx = lnast->add_child(while_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);

  Lnast_node inner_cond = emit_always_true_ref();  // recomputed per iteration
  auto       if_idx     = builder.add_child(Lnast_ntype::create_if());
  attach_loc(if_idx, loc);
  lnast->add_child(if_idx, inner_cond);
  auto then_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(then_idx);
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, then_idx);
  }
  builder.pop_stmts();
  auto else_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(else_idx);
  auto brk = builder.add_child(Lnast_ntype::create_func_break());
  lnast->add_child(brk, builder.mint_tmp_ref());
  builder.pop_stmts();

  builder.pop_stmts();
}

Prp2lnast::Initializer_scope_guard::Initializer_scope_guard(Prp2lnast& lower, TSNode n)
    : lower_(lower), destination_(lower.lnast.get()) {
  // `if` may have only an elif initializer; inspect every header field.
  for (uint32_t i = 0; i < ts_node_child_count(n); ++i) {
    const char* field = ts_node_field_name_for_child(n, i);
    if (!field || std::string_view(field) != "init") {
      continue;
    }
    capture_.emplace(lower_.capture_frames_, Capture_frame{});
    // A bare stmts Block does not merge writes to enclosing mutable bindings.
    // An always-taken if arm both bounds local visibility and exports those
    // writes through the existing if/SSA merge. uPass folds away the wrapper.
    auto scope = lower_.builder.add_child(Lnast_ntype::create_if());
    lower_.lnast->add_child(scope, Lnast_node::create_const("true"));
    lower_.builder.push_stmts(lower_.lnast->add_child(scope, Lnast_ntype::create_stmts()));
    break;
  }
}

Prp2lnast::Initializer_scope_guard::~Initializer_scope_guard() {
  // A diagnostic while finalizing a streamed nested lambda can unwind before
  // its destination is restored. Its builder does not own our saved cursor.
  if (capture_ && lower_.lnast.get() == destination_) {
    lower_.builder.pop_stmts();
  }
}

void Prp2lnast::process_while_statement(TSNode n) {
  Initializer_scope_guard init_scope(*this, n);
  TSNode                  cond = child_by_field(n, "condition");
  TSNode                  code = child_by_field(n, "code");
  TSNode                  init = child_by_field(n, "init");

  if (!ts_node_is_null(init)) {
    // Evaluate once, inside the initializer scope but outside the loop.
    for (TSNode c : ts_node_named_children(init)) {
      process_statement(c);
    }
  }

  // Infinite form — `while { … }` (no cond) or `while true { … }` (literal-true
  // cond) — shares the `loop {}` lowering (recomputed `1==1` ref condition).
  if (ts_node_is_null(cond) || trim(get_raw_text(cond)) == "true") {
    lower_infinite_loop(code, n);
    return;
  }

  // Pyrope loops are comptime-only — a `while` must fully unroll (codegen has
  // no runtime-loop lowering). Lower
  //   while cond { body }  ≡  while cond { if cond { body } else { break } }
  // The condition re-lowered INSIDE the body is re-evaluated every iteration
  // (the unroll re-walks the body), and the synthesized `break` terminates the
  // loop when it turns false. The outer `while` keeps the real condition so
  // typecheck still bool-checks it and a statically-false condition drops the
  // loop (zero iterations) without entering the body.
  Lnast_node cond_ref  = expr_to_node(cond);
  auto       while_idx = builder.add_child(Lnast_ntype::create_while());
  attach_loc(while_idx, n);  // span → upass/typecheck cond-not-bool can point here
  lnast->add_child(while_idx, cond_ref);
  auto body_idx = lnast->add_child(while_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);

  Lnast_node inner_cond = expr_to_node(cond);  // emitted into the body → recomputed per iteration
  auto       if_idx     = builder.add_child(Lnast_ntype::create_if());
  attach_loc(if_idx, cond);
  lnast->add_child(if_idx, inner_cond);

  auto then_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(then_idx);
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, then_idx);
  }
  builder.pop_stmts();

  auto else_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(else_idx);
  auto brk = builder.add_child(Lnast_ntype::create_func_break());
  lnast->add_child(brk, builder.mint_tmp_ref());
  builder.pop_stmts();

  builder.pop_stmts();
}

void Prp2lnast::process_for_statement(TSNode n) {
  // Pyrope `for` is comptime-only: every loop must fully unroll (there is no
  // runtime-loop lowering). prp2lnast is purely structural — it emits a raw
  // `for` LNAST node for EVERY loop and the upass runner does all expansion at
  // comptime (uPass_runner::unroll_for), beside the comb inliner. Keeping the
  // unroll in one place (the runner) avoids the parse-time/runner duplication
  // and lets `for x in args` / `for x in <runtime tuple>` unroll where the
  // iterable is bound.
  //
  // for-node layout — producer/consumer contract with uPass_runner::unroll_for:
  //   for( value_ref, iterable_ref, stmts(body), const(mode) [, idx_ref [, key_ref]] )
  //   - value_ref     : the value iteration variable.
  //   - iterable_ref  : a ref the runner resolves via constprop — a `range` tmp
  //     (try_range), a tuple name/tmp (try_tuple_shape), or a var-arg. Bare
  //     `NAME` / `ref NAME` emit a direct ref so a `ref` write-back targets the
  //     source tuple; ranges / inline tuples emit a tmp via expr_to_node (which
  //     lowers the range/tuple statements as preceding siblings).
  //   - mode          : "ref" for mutable-element iteration `for i in ref d`
  //     (the runner writes each (possibly mutated) value back into the slot
  //     after the body), else "val".
  //   - idx_ref/key_ref : the optional position / key bindings of
  //     `for (value, idx, key) in t` (idx present with 2+ binds, key with 3).
  //
  // read_is_visible already treats a `for` node as declaring its iterator, so no
  // visibility change is needed here.

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

  // Collect 1..3 binding ids. `for i in …` parses as a single typed_identifier;
  // `for (e, idx, key) in …` parses as typed_identifier_list with 1–3 items
  // (value, position, key).
  std::vector<TSNode> bind_ids;
  if (!ts_node_is_null(binding)) {
    std::string_view bt(ts_node_type(binding));
    if (bt == "typed_identifier") {
      TSNode id = child_by_field(binding, "identifier");
      if (!ts_node_is_null(id)) {
        bind_ids.push_back(id);
      }
    } else if (bt == "typed_identifier_list") {
      for (TSNode item : ts_node_named_children(binding)) {
        if (bind_ids.size() >= 3) {
          break;
        }
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

  // Is the iterable a range expression (`a..b`, `a..<b`, `a..+n`)? Detected
  // syntactically (independent of whether the bounds resolve at parse time) so a
  // parenthesized range `(0..<n)` — which parses as a single-item `tuple` — is
  // unwrapped to the inner range expr; otherwise expr_to_node would build a
  // 1-tuple instead of a range tmp.
  auto is_range_data = [&](TSNode d) -> bool {
    if (ts_node_is_null(d)) {
      return false;
    }
    TSNode           inner = d;
    std::string_view dt(ts_node_type(d));
    if (dt == "expression_list" || dt == "tuple") {
      uint32_t count = 0;
      TSNode   first{};
      for (uint32_t i = 0; i < child_count(d); i++) {
        const char* fn = ts_node_field_name_for_child(d, i);
        if (fn && std::string_view(fn) == "item") {
          if (count == 0) {
            first = child(d, i);
          }
          ++count;
        }
      }
      if (count != 1) {
        return false;
      }
      inner = first;
      dt    = std::string_view(ts_node_type(inner));
    }
    if (dt != "expression_item") {
      return false;
    }
    for (TSNode c : ts_node_named_children(inner)) {
      if (std::string_view(ts_node_type(c)) != "binary_other_op") {
        continue;
      }
      TSNode op_inner = ts_node_named_child(c, 0);
      if (ts_node_is_null(op_inner)) {
        continue;
      }
      std::string_view k(ts_node_type(op_inner));
      if (k == "op_range_inclusive" || k == "op_range_exclusive" || k == "op_range_count") {
        return true;
      }
    }
    return false;
  };

  if (bind_ids.empty() || ts_node_is_null(code) || ts_node_is_null(data)) {
    return;  // malformed `for` — nothing to lower
  }

  // Bindings first (value, then optional idx/key) so identifier_to_node records
  // them as for-declared before the body is walked.
  // Binding order: `for value in t`, or `for (index, value [, key]) in t`. A
  // PAIR means enumerate, with the index/position FIRST (like most languages,
  // not Ruby) — `index` is the const position, `value` is a copy of the element
  // (mutable only via `for (index, value) in ref t`, which writes it back), and
  // `key` is the field name for named tuples. Single-bind is just the value.
  // (Declare in source order; then assign roles.)
  std::vector<Lnast_node> bind_refs;
  bind_refs.reserve(bind_ids.size());
  for (auto bid : bind_ids) {
    bind_refs.push_back(identifier_to_node(bid, true));
  }
  const bool have_idx  = bind_refs.size() >= 2;
  const bool have_key  = bind_refs.size() >= 3;
  Lnast_node value_ref = have_idx ? bind_refs[1] : bind_refs[0];
  Lnast_node idx_ref   = have_idx ? bind_refs[0] : Lnast_node::create_invalid();
  Lnast_node key_ref   = have_key ? bind_refs[2] : Lnast_node::create_invalid();

  // Resolve the iterable into a single ref child + detect `ref` (write-back)
  // iteration. A bare `NAME` / `ref NAME` (possibly wrapped in a single-child
  // expression_list/expression_item) emits a direct ref so the runner's
  // write-back targets the source tuple; everything else (a range, an inline
  // tuple `(10,20,30)`) is lowered by expr_to_node, which emits the range/tuple
  // statements as siblings *before* the for node and returns the tmp ref.
  bool       is_ref = false;
  Lnast_node iterable_ref;
  {
    TSNode src = data;
    if (std::string_view(ts_node_type(src)) == "ref_identifier") {
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
      if ((st == "expression_list" || st == "expression_item") && ts_node_named_child_count(src) == 1) {
        src = ts_node_named_child(src, 0);
        continue;
      }
      break;
    }
    std::string_view st(ts_node_type(src));
    if (st == "identifier" || st == "ref_identifier") {
      iterable_ref = Lnast_node::create_ref(std::string(trim(get_text(src))));
    } else {
      // Range / inline-tuple / other expression. Unwrap a parenthesized range
      // (single-item `tuple`) to the inner range expr so expr_to_node lowers a
      // range tmp rather than a 1-tuple.
      TSNode iter_src = data;
      if (is_range_data(data)) {
        std::string_view rt(ts_node_type(iter_src));
        if (rt == "tuple" || rt == "expression_list") {
          for (uint32_t i = 0; i < child_count(iter_src); i++) {
            const char* fn = ts_node_field_name_for_child(iter_src, i);
            if (fn && std::string_view(fn) == "item") {
              iter_src = child(iter_src, i);
              break;
            }
          }
        }
      }
      iterable_ref = expr_to_node(iter_src);  // emits range/tuple stmts before the for; returns the tmp ref
    }
  }

  auto for_idx = builder.add_child(Lnast_ntype::create_for());
  attach_loc(for_idx, n);  // span at the `for` → shadow/non-comptime-iterable diagnostics locate here
  lnast->add_child(for_idx, value_ref);
  lnast->add_child(for_idx, iterable_ref);
  auto body_idx = lnast->add_child(for_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);
  {
    // The loop binds hold one value per (unrolled) iteration. The index and
    // the key are comptime, and so is the value of a comptime iterable (a
    // range, a tuple of comptime values): a lambda nested in the body gets
    // them as generics (see capture_frames_). The value of a runtime iterable
    // (`for x in (a, b)`) is a runtime binding, which no nested lambda reads.
    Capture_frame_guard binds(capture_frames_, Capture_frame{});
    const bool          comptime_value = is_range_data(data) || is_comptime_expression(data);
    for (size_t i = 0; i < bind_ids.size(); ++i) {
      const bool is_value = i == (have_idx ? 1 : 0);
      note_binding(canonical_escaped_ident(trim(get_text(bind_ids[i]))),
                   !is_value || comptime_value ? Bind_kind::generic : Bind_kind::runtime);
    }
    process_scope_statement(code, body_idx);
  }
  builder.pop_stmts();

  // Trailing metadata (after the body): iteration mode + optional idx/key binds.
  lnast->add_child(for_idx, Lnast_node::create_const(is_ref ? "ref" : "val"));
  if (have_idx) {
    lnast->add_child(for_idx, idx_ref);
  }
  if (have_key) {
    lnast->add_child(for_idx, key_ref);
  }
}

void Prp2lnast::process_loop_statement(TSNode n) {
  // `loop { body }` ≡ `while (1==1) { if (1==1) {body} else {break} }` — the
  // recomputed-ref infinite form (the body must `break` to terminate).
  lower_infinite_loop(child_by_field(n, "code"), n);
}

// `tick [N] [clocks=(…)] [resets=(…)] { body }` — the simulation cycle loop of a
// `test` block. Emits (count, stmts-body) under a `tick` node.
//
// WHY IT IS ITS OWN NODE rather than a `while`/`for`. Those are comptime-only:
// the upass runner FULLY UNROLLS them (unroll_for / unroll_while), which is
// exactly wrong here. A tick's iteration count is assumed UNKNOWN — always,
// including when written as a literal — because it is routinely overridden by a
// runtime `--arg`. The runner walks a tick body in an uncertain scope so every
// variable written inside is invalidated on exit (see lnast_nodes.def).
//
// Until this existed, `tick_statement` had no dispatch entry and fell through to
// the unhandled-statement fallback, so the ENTIRE body was dropped on the way to
// LNAST while `prp_sim.cpp`'s independent CST walk still executed it. That split
// is what made a testbench `assert` fold against a variable's stale initializer
// (lhdsuite fixme issue 2).
//
// `clocks=` / `resets=` are deliberately NOT lowered: they configure the VCD
// waveform and are consumed only by prp_sim's CST walk, so putting them in LNAST
// would add names no upass consumer reads.
void Prp2lnast::process_tick_statement(TSNode n) {
  TSNode count = child_by_field(n, "value");
  TSNode code  = child_by_field(n, "code");

  // A nested `tick` is rejected HERE, not left to `lhd sim`. Both ticks
  // synthesize the same implicit loop variable (`clock` by default), so the
  // inner declaration trips upass.semacheck's no-shadowing rule FIRST and the
  // user sees "declaration of 'clock' shadows an outer-scope variable" — a name
  // that appears nowhere in their source. prp2lnast exempts the synthesized
  // decl from its own shadow check (tick_loop_var_decls_), but semacheck runs
  // later with its own check and cannot see that set, so the exemption alone is
  // not enough. Reporting at parse time keeps the honest message and the right
  // source span.
  // docs 05b "Running cycles (tick)": `tick` is usable only inside `test`.
  if (simulation_test_depth_ == 0) {
    report_error(n,
                 "tick-outside-test",
                 "syntax",
                 "`tick` is only allowed inside a `test` block",
                 "a design body has no testbench clock: move the loop into a `test` block");
    return;
  }
  if (in_tick_statement_) {
    report_error(n,
                 "tick-nested",
                 "unsupported",
                 "a `tick` cannot be nested inside another `tick` (single-clock model)",
                 "flatten into one tick loop: a single `tick` body advances the shared clock with `step`");
    return;
  }
  in_tick_statement_ = true;

  // Lower the count OUTSIDE the body: it is evaluated once, before the loop, so
  // it must not land in the uncertain scope the body gets.
  Lnast_node count_ref = ts_node_is_null(count) ? Lnast_node::create_const("") : expr_to_node(count);

  auto tick_idx = builder.add_child(Lnast_ntype::create_tick());
  attach_loc(tick_idx, n);
  lnast->add_child(tick_idx, count_ref);

  auto body_idx = lnast->add_child(tick_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);
  tick_extra_steps_.clear();
  if (!ts_node_is_null(code)) {
    (void)tick_path_steps(code, 0);
  }

  // THE LOOP VARIABLE. A tick body implicitly binds the 0-based cycle index —
  // `clock` by default, renamable through the `clocks=(name=ratio)` clause — and
  // fixtures read it freely (`acc.reset = clock < 2`). Nothing in the source
  // declares it, so it must be introduced here.
  //
  // It is needed even though the RUNNER emits a tick body verbatim without
  // folding: semacheck's scope-aware undefined-read check walks the whole tree
  // independently, so an undeclared `clock` is a hard error regardless (removing
  // this declaration fails ~75 tests with "read of undefined variable 'clock'").
  //
  // Seeded with the TYPELESS UNKNOWN literal `0sb?` (Kind::unknown, the wildcard
  // that skips checks and never errors) rather than a sized value. That is both
  // honest and convenient: the index genuinely is not comptime (it differs every
  // iteration, and R0 says the count is unknown anyway), and a typeless unknown
  // will not fight the surrounding widths — `acc.din = clock` into a u8 port and
  // `11 + 17*clock` must both stay legal.
  //
  // CAVEAT: this puts a binding in the tree that no source line wrote, so a
  // Pyrope round-trip of a `test` block would emit it as real code. No writer
  // walks test bodies today; revisit if one starts to.
  const std::string loop_var = tick_loop_var_name(n);
  auto              decl_idx = builder.add_child(Lnast_ntype::create_declare());
  attach_loc(decl_idx, n);
  lnast->add_child(decl_idx, Lnast_node::create_ref(loop_var));
  // declare is (var, type, mode) — the type/mode children are NOT optional.
  // Untyped (`prim_type_none`, so the `0sb?` seed below stays typeless) and
  // `mut` (the index takes a new value every iteration).
  lnast->add_child(decl_idx, Lnast_ntype::create_prim_type_none());
  lnast->add_child(decl_idx, Lnast_node::create_const("mut"));
  // …but EXEMPT from the no-shadowing rule: `mut` makes it shadow-checkable, and
  // a test parameter named like the loop var (`test t(clock:u8=3)`) would then
  // report generic shadowing instead of the named "collides with a test
  // parameter". Nothing in the source declares this name, so the generic
  // "rename the inner/loop variable" hint points at a line that is not there.
  tick_loop_var_decls_.insert(decl_idx);
  (void)note_binding(loop_var, Bind_kind::runtime);  // differs every iteration: never comptime
  auto seed_idx = builder.add_child(Lnast_ntype::create_store());
  attach_loc(seed_idx, n);
  lnast->add_child(seed_idx, Lnast_node::create_ref(loop_var));
  lnast->add_child(seed_idx, Lnast_node::create_const("0sb?"));

  if (!ts_node_is_null(code)) {
    process_scope_statement(code, body_idx);
  }
  in_tick_statement_ = false;  // ticks never nest (guarded above), so a plain reset is exact
  builder.pop_stmts();
}

// The tick loop-variable name: the lvalue of the single `clocks=(name=ratio)`
// entry, else `clock`. Mirrors prp_sim.cpp's tick_one_entry, which is the
// authority at simulation time — the two must agree on the name or a fixture
// that renames its counter would read an undeclared variable here while
// simulating fine.
std::string Prp2lnast::tick_loop_var_name(TSNode tick) {
  TSNode clocks = child_by_field(tick, "clocks");
  if (!ts_node_is_null(clocks)) {
    for (TSNode a : ts_node_named_children(clocks)) {
      if (std::string_view(ts_node_type(a)) != "assignment") {
        continue;  // skip the operator wrapper / stray nodes
      }
      TSNode lv = child_by_field(a, "lvalue");
      if (!ts_node_is_null(lv)) {
        // Strip any type annotation: `clock:u4` -> `clock`, same as prp_sim.
        std::string nm{trim(get_text(lv))};
        if (const auto colon = nm.find(':'); colon != std::string::npos) {
          nm = std::string{trim(std::string_view(nm).substr(0, colon))};
        }
        if (!nm.empty()) {
          return nm;
        }
      }
    }
  }
  return "clock";
}

// The most `step`s on any path through `n`, entered with `before` already
// taken on this path: a statement sequence adds up, an `if`/`match` takes its
// largest arm (arms are alternatives), and a nested function body is its own
// world. Every step that makes a path's count exceed one is recorded in
// tick_extra_steps_ (the tick-two-steps diagnostic).
uint32_t Prp2lnast::tick_path_steps(TSNode n, uint32_t before) {
  if (ts_node_is_null(n)) {
    return before;
  }
  std::string_view t(ts_node_type(n));
  if (t == "step_statement") {
    if (before >= 1) {
      tick_extra_steps_.insert(ts_node_start_byte(n));
    }
    return before + 1;
  }
  if (t == "lambda" || t == "tick_statement") {
    return before;  // a comb's body / a (rejected) nested tick
  }
  if (t == "if_expression" || t == "match_expression") {
    uint32_t most = before;
    for (TSNode c : ts_node_named_children(n)) {
      most = std::max(most, tick_path_steps(c, before));  // each arm starts from `before`
    }
    return most;
  }
  uint32_t cur = before;
  for (TSNode c : ts_node_named_children(n)) {
    cur = tick_path_steps(c, cur);
  }
  return cur;
}

// `step [N]` — advance N simulation cycles (default 1). A leaf whose only child
// is the count, so `step`, `step 5` and `step(1000)` all lower alike.
void Prp2lnast::process_step_statement(TSNode n) {
  TSNode count = child_by_field(n, "value");

  // docs 05b "Test only statements": `step [ncycles]` is not available outside `test` blocks.
  if (simulation_test_depth_ == 0) {
    report_error(n,
                 "step-outside-test",
                 "syntax",
                 "`step` is only allowed inside a `test` block",
                 "a design body has no testbench clock: advance simulation cycles from a `test` block");
    return;
  }

  if (in_tick_statement_ && tick_extra_steps_.contains(ts_node_start_byte(n))) {
    report_error(n,
                 "tick-two-steps",
                 "syntax",
                 "a `tick` iteration has exactly one `step`, this is the second",
                 "each tick iteration is one cycle (the minted `clock` counts one per iteration): use `step N` or a "
                 "second `tick` for more cycles");
    return;
  }

  auto step_idx = builder.add_child(Lnast_ntype::create_step());
  attach_loc(step_idx, n);
  lnast->add_child(step_idx, ts_node_is_null(count) ? Lnast_node::create_const("1") : expr_to_node(count));
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
  // `return` is a terminator only — it never carries a value (assign outputs
  // first, then `return`).
  emit_marker(Lnast_ntype::create_func_return(), Lnast_node::create_invalid());
}

std::vector<Prp2lnast::Call_arg> Prp2lnast::collect_call_args(TSNode arg_tuple) {
  std::vector<Call_arg> call_args;
  if (ts_node_is_null(arg_tuple)) {
    return call_args;
  }

  // The statement form of a call wraps the call's
  // argument tuple in an outer `expression_list`. Unwrap to the inner tuple
  // so the loop below iterates the real arg children (not a single
  // wrapped-tuple "arg" that would emit `f((a,b))` instead of `f(a,b)`).
  if (std::string_view(ts_node_type(arg_tuple)) == "expression_list" && ts_node_named_child_count(arg_tuple) == 1) {
    TSNode           inner = ts_node_named_child(arg_tuple, 0);
    std::string_view it(ts_node_type(inner));
    if (it == "tuple" || it == "tuple_sq") {
      arg_tuple = inner;
    }
  }

  call_args.reserve(ts_node_named_child_count(arg_tuple));
  for (TSNode c : ts_node_named_children(arg_tuple)) {
    std::string_view t(ts_node_type(c));

    Call_arg arg;
    if (t == "assignment" || t == "arg_assignment") {
      // `arg_assignment` is the call-site named-binding node (`name = expr`,
      // dotted names allowed); plain `assignment` still reaches here through
      // data-tuple shapes (e.g. `function_call_type` arguments).
      arg.is_assign = true;
      TSNode lv     = child_by_field(c, "lvalue");
      TSNode rv     = child_by_field(c, "rvalue");

      std::string_view lvt(ts_node_type(lv));
      if (lvt == "typed_identifier") {
        TSNode id = child_by_field(lv, "identifier");
        if (!ts_node_is_null(id)) {
          // Canonicalize so a keyword-escaped call-site key (`` `in` = x ``)
          // matches the parameter DECLARATION, which is stored unescaped (`in`).
          arg.assign_key = str_tools::canonical_escaped_path(trim(get_text(id)));
        }
      } else {
        arg.assign_key = str_tools::canonical_escaped_path(trim(get_text(lv)));
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
    } else if (t == "ref_identifier") {
      arg.is_ref = true;
      arg.value  = expr_to_node(c);
    } else if (t == "unary_expression" && [&] {
                 TSNode op_n = child_by_field(c, "operator");
                 return !ts_node_is_null(op_n) && std::string_view(ts_node_type(op_n)) == "op_spread";
               }()) {
      // `...rest` call-argument spread: capture the inner bundle ref and mark
      // it so the runner expands rest's fields into named/positional actuals.
      TSNode arg_n  = child_by_field(c, "argument");
      arg.is_spread = true;
      arg.value     = expr_to_node(arg_n);
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

std::vector<Prp2lnast::Generic_call_arg> Prp2lnast::collect_generic_args(TSNode call_node) {
  // Explicit call-site generic bindings (`f<int,string>(…)`, grammar field
  // `generic` → generic_type_list). Lower each type argument BEFORE the
  // fcall node is created: a primitive/constrained type becomes a
  // `declare(tmp, prim_type_*, 'type')` statement (the int-type-call shape —
  // see does_operand_to_node) whose ref rides the marker; a named type
  // passes its ref through directly. A NAMED bind (`f<T=u8>`, todo 3g C)
  // arrives as an `arg_assignment` (lvalue=name, rvalue=type): the name rides
  // alongside the lowered value.
  std::vector<Generic_call_arg> out;
  TSNode                        gen = child_by_field(call_node, "generic");
  if (ts_node_is_null(gen)) {
    return out;
  }
  // Lower one type-argument node (`u8`, `3`, `Byte`, `inc`) into a ref/const.
  auto lower_one = [&](TSNode ty) -> Lnast_node {
    std::string_view tt(ts_node_type(ty));
    // A bare POSTFIX attribute read (`f<N=x.[bits]>`): prpparse builds it like
    // its expression spelling, so it lowers like any attribute read and the tmp
    // folds to the comptime constant the generic binds.
    if (tt == "attribute_read") {
      return expr_to_node(ty);
    }
    if (tt == "expression_type" || tt == "dot_expression_type") {
      // A COMPOUND value expression (`f<N=(SIZE >> 1)>`, `f<N=(lvl + 1)>`)
      // reaches here as an `expression_type` wrapping a parenthesized `tuple`
      // (the generic rvalue is parsed with the TYPE grammar, so an expression
      // must be parenthesized: a bare `>` would close the generic list). Its
      // source text is not an identifier, so passing it through as a ref
      // spelled `(SIZE >> 1)` builds a malformed ref that pass.lnastfmt
      // rejects. Lower it through the ordinary expression path instead: the
      // statements land ahead of the fcall and the tmp folds to the comptime
      // constant the generic binds (resolve_generic_binds folds the ref).
      TSNode inner = ts_node_named_child_count(ty) == 1 ? ts_node_named_child(ty, 0) : TSNode{};
      if (!ts_node_is_null(inner)) {
        std::string_view it(ts_node_type(inner));
        // prpparse's parse_type() reaches a non-identifier type expression
        // through parse_paren / parse_constant / parse_if_expression /
        // parse_match_expression, so the parenthesized form lands as either
        // `paren_group` or `tuple` depending on whether a suffix followed.
        if (it == "paren_group" || it == "tuple" || it == "if_expression" || it == "match_expression") {
          return expr_to_node(inner);
        }
      }
      auto txt = trim(get_text(ty));
      if (!lookup_capture(canonical_escaped_ident(txt))) {
        check_type_name_spelling(ty);  // `f<T=i8>`, unless a value is named `i8`
      }
      record_name_reads(ty, Generic_read::argument);  // passed through by name: the runner binds the entity
      // A CONSTANT-valued generic (`f<3>`, `f<true>`, `f<'s'>`) rides as a
      // const, not a ref — a ref named `3` is malformed (lnastfmt) and the
      // runner substitutes the literal for body reads of the generic (todo 3g
      // A/D). A named type / lambda stays a ref (resolved at bind time).
      const char c0 = txt.empty() ? '\0' : txt.front();
      const bool is_const_arg
          = std::isdigit(static_cast<unsigned char>(c0)) || c0 == '\'' || c0 == '"' || txt == "true" || txt == "false";
      return is_const_arg ? Lnast_node::create_const(std::string(txt)) : Lnast_node::create_ref(std::string(txt));
    }
    // A bound that does not fold here (`f<T=unsigned(bits=N)>` in a generic
    // body) is computed by statements ahead of the declare, like a body
    // variable's; the runner folds them once the caller's generics are bound.
    prelower_int_type_bounds(ty);
    auto didx = builder.add_child(Lnast_ntype::create_declare());
    auto tref = builder.mint_tmp_ref();
    lnast->add_child(didx, tref);
    emit_type_expr(didx, ty);
    lnast->add_child(didx, Lnast_node::create_const("type"));
    return tref;
  };
  for (TSNode item : ts_node_named_children(gen)) {
    if (std::string_view(ts_node_type(item)) == "arg_assignment") {
      TSNode      lv = child_by_field(item, "lvalue");  // generic NAME
      TSNode      rv = child_by_field(item, "rvalue");  // bound type/const/lambda
      std::string nm = ts_node_is_null(lv) ? std::string{} : std::string(trim(get_text(lv)));
      out.push_back({lower_one(rv), std::move(nm)});
    } else {
      out.push_back({lower_one(item), {}});
    }
  }
  return out;
}

void Prp2lnast::add_generic_args_to_fcall(const Lnast_nid& fcall_idx, const std::vector<Generic_call_arg>& generic_args) {
  for (const auto& g : generic_args) {
    auto aidx = lnast->add_child(fcall_idx, Lnast_ntype::create_store());
    lnast->add_child(aidx, Lnast_node::create_ref(call_generic_arg_marker));
    lnast->add_child(aidx, g.value);
    // A named bind carries the target generic name as a trailing const child;
    // gather_actuals reads it to bind by name (todo 3g C).
    if (!g.name.empty()) {
      lnast->add_child(aidx, Lnast_node::create_const(g.name));
    }
  }
}

void Prp2lnast::add_call_args_to_fcall(const Lnast_nid& fcall_idx, const std::vector<Call_arg>& call_args) {
  for (const auto& arg : call_args) {
    if (arg.is_ufcs) {
      auto aidx = lnast->add_child(fcall_idx, Lnast_ntype::create_store());
      lnast->add_child(aidx, Lnast_node::create_ref(call_ufcs_arg_marker));
      lnast->add_child(aidx, arg.value);
    } else if (arg.is_ref) {
      auto aidx = lnast->add_child(fcall_idx, Lnast_ntype::create_store());
      lnast->add_child(aidx, Lnast_node::create_ref(call_ref_arg_marker));
      lnast->add_child(aidx, arg.value);
    } else if (arg.is_spread) {
      auto aidx = lnast->add_child(fcall_idx, Lnast_ntype::create_store());
      lnast->add_child(aidx, Lnast_node::create_ref(call_spread_arg_marker));
      lnast->add_child(aidx, arg.value);
    } else if (arg.is_assign) {
      auto aidx = lnast->add_child(fcall_idx, Lnast_ntype::create_store());
      lnast->add_child(aidx, Lnast_node::create_ref(arg.assign_key));
      lnast->add_child(aidx, arg.value);
    } else {
      lnast->add_child(fcall_idx, arg.value);
    }
  }
}

std::string Prp2lnast::streamed_actuals_key(std::string_view scope_unit, std::string_view callee) {
  std::string key(scope_unit);
  key.push_back('\n');  // cannot appear in a unit or entity name
  key.append(callee);
  return key;
}

void Prp2lnast::append_streamed_capture_actuals(const Lnast_nid& fcall, std::string_view callee) {
  // Actuals are registered under the DEFINING scope's unit name (see
  // streamed_actuals_key): a bare-name lookup let scope A's capture list ride
  // calls to a DIFFERENT same-named helper in scope B, where those names do
  // not exist. Resolve lexically: walk the current destination's unit-name
  // prefix chain innermost-first.
  auto resolve = [&]() {
    auto it = streamed_capture_actuals_.end();
    for (std::string_view unit = lnast->get_top_module_name();;) {
      it = streamed_capture_actuals_.find(streamed_actuals_key(unit, callee));
      if (it != streamed_capture_actuals_.end()) {
        return it;
      }
      const auto dot = unit.rfind('.');
      if (dot == std::string_view::npos) {
        return streamed_capture_actuals_.end();
      }
      unit = unit.substr(0, dot);
    }
  };
  auto it = resolve();
  if (it == streamed_capture_actuals_.end()) {
    return;
  }
  // Generics this call already binds by name (`f<W=3>(…)` wins).
  absl::flat_hash_set<std::string> present;
  for (auto c = lnast->get_first_child(fcall); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (!Lnast_ntype::is_store(lnast->get_type(c))) {
      continue;
    }
    auto key = lnast->get_first_child(c);
    auto val = key.is_invalid() ? key : lnast->get_sibling_next(key);
    auto gen = val.is_invalid() ? val : lnast->get_sibling_next(val);
    if (!gen.is_invalid() && lnast->get_name(key) == call_generic_arg_marker) {
      present.insert(std::string(lnast->get_name(gen)));
    }
  }
  for (const auto& name : it->second) {
    if (!present.insert(name).second) {
      continue;
    }
    // The implicit generic binds to the caller's own value of the name.
    auto arg = lnast->add_child(fcall, Lnast_ntype::create_store());
    lnast->add_child(arg, Lnast_node::create_ref(call_generic_arg_marker));
    lnast->add_child(arg, Lnast_node::create_ref(name));
    lnast->add_child(arg, Lnast_node::create_const(name));
    // A caller that is itself nested now reads the enclosing generic too.
    if (const auto hit = lookup_capture(name); hit && hit->innermost_crossed && hit->kind == Bind_kind::generic) {
      note_implicit_generic(*hit, name);
    }
  }
}

void Prp2lnast::patch_streamed_capture_calls(const std::shared_ptr<Lnast>& target, std::string_view callee,
                                             const std::vector<std::string>& captures) {
  if (!target || captures.empty()) {
    return;
  }
  if (target == lnast) {
    capture_rw_.clear();  // the patched calls read more names now
  }
  auto saved = lnast;
  lnast      = target;
  for (auto n : target->depth_preorder(target->get_root())) {
    if (n.is_invalid() || !Lnast_ntype::is_func_call(target->get_type(n))) {
      continue;
    }
    auto dst = target->get_first_child(n);
    auto fn  = dst.is_invalid() ? dst : target->get_sibling_next(dst);
    if (!fn.is_invalid() && target->get_name(fn) == callee) {
      append_streamed_capture_actuals(n, callee);
    }
  }
  lnast = std::move(saved);
}

namespace {
// Names a statement WRITES and READS, for the nested-lambda capture slice
// (plan_streamed_captures). A statement's first ref is its destination (store,
// declare, attr_set, type_spec, an op's dst, a call's dst); a field-path store
// or a set_mask also reads it. Loop/if nodes contribute their whole subtree.
// The first child of a `store` under a `tuple_add`/`func_call` is a field or
// argument KEY, never a variable.
bool prp_is_key_store(const Lnast& ln, const Lnast_nid& nid) {
  if (!Lnast_ntype::is_store(ln.get_type(nid))) {
    return false;
  }
  const auto p = ln.get_parent(nid);
  return !p.is_invalid() && (Lnast_ntype::is_tuple_add(ln.get_type(p)) || Lnast_ntype::is_func_call(ln.get_type(p)));
}

void prp_collect_reads(const Lnast& ln, const Lnast_nid& nid, absl::flat_hash_set<std::string>& reads) {
  if (Lnast_ntype::is_ref(ln.get_type(nid))) {
    reads.insert(std::string(ln.get_name(nid)));
    return;
  }
  const bool keyed = prp_is_key_store(ln, nid);
  bool       first = true;
  for (auto c : ln.children(nid)) {
    if (!(keyed && first)) {
      prp_collect_reads(ln, c, reads);
    }
    first = false;
  }
}

// The variable a call actual `val` names: a ref, or the root of the
// `tuple_get` temp an earlier sibling of the call built for a multi-level
// UFCS receiver (`a.b.inc()`).
std::optional<std::string> prp_call_actual_root(const Lnast& ln, const Lnast_nid& call, const Lnast_nid& val) {
  if (val.is_invalid() || !Lnast_ntype::is_ref(ln.get_type(val))) {
    return std::nullopt;
  }
  std::string name(ln.get_name(val));
  for (auto s = ln.get_sibling_prev(call); Lnast::is_tmp(name) && !s.is_invalid(); s = ln.get_sibling_prev(s)) {
    const auto dst = ln.get_first_child(s);
    if (!Lnast_ntype::is_tuple_get(ln.get_type(s)) || dst.is_invalid() || ln.get_name(dst) != name) {
      continue;
    }
    const auto base = ln.get_sibling_next(dst);
    if (base.is_invalid() || !Lnast_ntype::is_ref(ln.get_type(base))) {
      return std::nullopt;
    }
    name = ln.get_name(base);
  }
  if (Lnast::is_tmp(name)) {
    return std::nullopt;
  }
  return name;
}

void prp_stmt_rw(const Lnast& ln, const Lnast_nid& stmt, Prp_stmt_rw& rw,
                 const std::function<bool(std::string_view)>& ufcs_writes_receiver) {
  const auto t = ln.get_type(stmt);
  if (Lnast_ntype::is_stmts(t)) {
    for (auto c : ln.children(stmt)) {
      prp_stmt_rw(ln, c, rw, ufcs_writes_receiver);
    }
    return;
  }
  if (Lnast_ntype::is_if_like(t) || Lnast_ntype::is_while(t)) {
    for (auto c : ln.children(stmt)) {
      if (Lnast_ntype::is_stmts(ln.get_type(c))) {
        prp_stmt_rw(ln, c, rw, ufcs_writes_receiver);
      } else {
        prp_collect_reads(ln, c, rw.reads);
      }
    }
    return;
  }
  if (Lnast_ntype::is_for(t)) {
    // for(value, iterable, stmts, mode [, idx [, key]]): the binds are declared.
    int pos = 0;
    for (auto c = ln.get_first_child(stmt); !c.is_invalid(); c = ln.get_sibling_next(c), ++pos) {
      const auto ct = ln.get_type(c);
      if (Lnast_ntype::is_stmts(ct)) {
        prp_stmt_rw(ln, c, rw, ufcs_writes_receiver);
      } else if (Lnast_ntype::is_ref(ct) && pos == 1) {
        rw.reads.insert(std::string(ln.get_name(c)));
      } else if (Lnast_ntype::is_ref(ct)) {
        rw.writes.insert(std::string(ln.get_name(c)));
        rw.declares.insert(std::string(ln.get_name(c)));  // a loop bind is scoped to the loop
      }
    }
    return;
  }
  const bool no_dst = Lnast_ntype::is_cassert(t) || Lnast_ntype::is_timecheck(t) || Lnast_ntype::is_func_break(t)
                      || Lnast_ntype::is_func_continue(t) || Lnast_ntype::is_func_return(t);
  auto c = ln.get_first_child(stmt);
  if (Lnast_ntype::is_func_call(t) && !c.is_invalid()) {
    const auto fn = ln.get_sibling_next(c);
    if (!fn.is_invalid() && Lnast_ntype::is_ref(ln.get_type(fn))) {
      rw.callees.insert(std::string(ln.get_name(fn)));
    }
    // A `ref x` actual, and the receiver of a `ref self` method, is written
    // back by the call.
    for (auto a = fn.is_invalid() ? fn : ln.get_sibling_next(fn); !a.is_invalid(); a = ln.get_sibling_next(a)) {
      const auto key = Lnast_ntype::is_store(ln.get_type(a)) ? ln.get_first_child(a) : Lnast_nid{};
      if (key.is_invalid()) {
        continue;
      }
      const auto k = ln.get_name(key);
      if (k == call_ref_arg_marker
          || (k == call_ufcs_arg_marker && Lnast_ntype::is_ref(ln.get_type(fn)) && ufcs_writes_receiver(ln.get_name(fn)))) {
        if (auto root = prp_call_actual_root(ln, stmt, ln.get_sibling_next(key))) {
          rw.writes.insert(std::move(*root));
        }
      }
    }
  }
  if (!no_dst && !c.is_invalid() && Lnast_ntype::is_ref(ln.get_type(c))) {
    std::string dst(ln.get_name(c));
    // `declare(x, …)`, or the pre-merge cluster head `attr_set(x, 'type', mode)`.
    if (const auto key = ln.get_sibling_next(c);
        Lnast_ntype::is_declare(t) || (Lnast_ntype::is_attr_set(t) && !key.is_invalid() && ln.get_name(key) == "type")) {
      rw.declares.insert(dst);
    }
    // A field-path store (3+ children) or a set_mask updates part of dst.
    const auto val = ln.get_sibling_next(c);
    if (Lnast_ntype::is_set_mask(t) || (Lnast_ntype::is_store(t) && !val.is_invalid() && !ln.get_sibling_next(val).is_invalid())) {
      rw.reads.insert(dst);
    }
    rw.writes.insert(std::move(dst));
    c = ln.get_sibling_next(c);
  }
  for (; !c.is_invalid(); c = ln.get_sibling_next(c)) {
    prp_collect_reads(ln, c, rw.reads);
  }
}

// A `type`/`enum` declaration (declare or the pre-merge attr_set cluster with a
// non-variable mode): its name is a hoisted comptime entity the lambda already
// sees, never something to replay.
std::optional<std::string_view> prp_declared_type_name(const Lnast& ln, const Lnast_nid& stmt) {
  const auto t = ln.get_type(stmt);
  if (!Lnast_ntype::is_declare(t) && !Lnast_ntype::is_attr_set(t)) {
    return std::nullopt;
  }
  const auto c0 = ln.get_first_child(stmt);
  const auto c1 = c0.is_invalid() ? c0 : ln.get_sibling_next(c0);
  const auto c2 = c1.is_invalid() ? c1 : ln.get_sibling_next(c1);
  if (c2.is_invalid() || !Lnast_ntype::is_ref(ln.get_type(c0)) || (Lnast_ntype::is_attr_set(t) && ln.get_name(c1) != "type")) {
    return std::nullopt;
  }
  const auto mode = ln.get_name(c2);
  if (mode == "mut" || mode == "const" || mode == "reg" || mode == "stage" || mode == "wire" || mode.starts_with("mut ")
      || mode.starts_with("const ")) {
    return std::nullopt;
  }
  return ln.get_name(c0);
}

// The statements of `ln` visible at the builder cursor `stmts`, in program
// order: every child of the innermost open frame, and of each enclosing frame
// the children before the statement that holds the next inner frame.
void prp_visible_stmts(const Lnast& ln, const Lnast_nid& stmts, std::vector<Lnast_nid>& out) {
  std::vector<std::pair<Lnast_nid, Lnast_nid>> frames;  // (stmts, first invisible child)
  Lnast_nid                                    stop;
  for (Lnast_nid s = stmts; !s.is_invalid();) {
    frames.emplace_back(s, stop);
    Lnast_nid child = s;
    Lnast_nid p     = ln.get_parent(s);
    while (!p.is_invalid() && !Lnast_ntype::is_stmts(ln.get_type(p))) {
      child = p;
      p     = ln.get_parent(p);
    }
    stop = child;
    s    = p;
  }
  for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
    for (auto c = ln.get_first_child(it->first); !c.is_invalid() && c != it->second; c = ln.get_sibling_next(c)) {
      out.push_back(c);
    }
  }
}

// Copy a captured statement into a streamed lambda's prologue, renaming the
// variable refs in `rename` (field/argument keys keep their text).
void prp_copy_capture(const Lnast& src, const Lnast_nid& nid, Lnast& dst, const Lnast_nid& parent,
                      const absl::flat_hash_map<std::string, std::string>& rename, bool is_key) {
  const auto t = src.get_type(nid);
  Lnast_nid  nn;
  if (Lnast_ntype::is_ref(t)) {
    const auto name = src.get_name(nid);
    const auto it   = is_key ? rename.end() : rename.find(name);
    nn              = dst.add_child(parent, Lnast_node::create_ref(it == rename.end() ? std::string(name) : it->second));
  } else if (Lnast_ntype::is_const(t)) {
    nn = dst.add_child(parent, Lnast_node::create_const(src.get_name(nid)));
  } else {
    nn = dst.add_child(parent, t);
  }
  if (Lnast::srcid_carries(t)) {
    if (const auto id = src.get_srcid(nid); id != hhds::SourceId_invalid) {
      dst.set_srcid(nn, dst.source_locator().import_from(src.source_locator(), id));
    }
  }
  const bool keyed = prp_is_key_store(src, nid);
  bool       first = true;
  for (auto c : src.children(nid)) {
    prp_copy_capture(src, c, dst, nn, rename, keyed && first);
    first = false;
  }
}
}  // namespace

Prp2lnast::Binding& Prp2lnast::note_binding(std::string_view name, Bind_kind kind) {
  auto& b = capture_frames_.back().names[std::string(name)];
  b       = Binding{.kind = kind, .decl_depth = conditional_depth_};
  return b;
}

std::optional<Prp2lnast::Capture_lookup> Prp2lnast::lookup_capture(std::string_view name) const {
  std::optional<size_t> crossed;
  for (size_t i = capture_frames_.size(); i-- > 0;) {
    const auto& f = capture_frames_[i];
    if (const auto it = f.names.find(name); it != f.names.end()) {
      return Capture_lookup{.kind = it->second.kind, .frame = i, .innermost_crossed = crossed, .binding = &it->second};
    }
    if (f.lambda_boundary && !crossed) {
      crossed = i;
    }
  }
  return std::nullopt;
}

Prp2lnast::Binding* Prp2lnast::find_binding(std::string_view name) {
  for (size_t i = capture_frames_.size(); i-- > 0;) {
    if (const auto it = capture_frames_[i].names.find(name); it != capture_frames_[i].names.end()) {
      return &it->second;
    }
  }
  return nullptr;
}

void Prp2lnast::check_capture_read(std::string_view name, const TSNode& at) const {
  const auto hit = lookup_capture(name);
  if (!hit || !hit->innermost_crossed) {
    return;
  }
  const auto in_lambda = [](const Capture_frame& f) {
    return f.lambda_name.empty() ? std::string{"an anonymous lambda"} : std::format("lambda '{}'", f.lambda_name);
  };
  if (binding_visible_in_lambda(*hit->binding)) {
    // A comptime const, or a fold_const (user ruling 2026-09-28 (32)): visible
    // unless replaying its computation reaches a runtime root.
    for (size_t j = capture_frames_.size(); j-- > hit->frame + 1;) {
      const auto& f  = capture_frames_[j];
      const auto  it = f.uncapturable.find(name);
      if (it == f.uncapturable.end()) {
        continue;
      }
      if (hit->binding->fold_const) {
        const auto& lambda = f.lambda_name;
        report_error(at,
                     "nested-runtime-capture",
                     "name",
                     std::format("'{}' is a runtime value of the enclosing scope; it is not visible inside {} (its value "
                                 "depends on '{}', so it does not fold to a compile-time constant)",
                                 name,
                                 in_lambda(f),
                                 it->second),
                     std::format("a nested lambda sees an enclosing value only when it folds to a compile-time constant; "
                                 "pass '{}' as an input of {}",
                                 name,
                                 lambda.empty() ? std::string{"the lambda"} : std::format("'{}'", lambda)));
      } else {
        report_error(at,
                     "nested-runtime-capture",
                     "name",
                     std::format("'{}' is computed from '{}', a runtime value of the enclosing scope; it is not visible inside {}",
                                 name,
                                 it->second,
                                 in_lambda(f)),
                     std::format("a nested lambda sees only the comptime values it can recompute from comptime bindings; "
                                 "compute '{}' inside {}, or pass it as an input",
                                 name,
                                 in_lambda(f)));
      }
    }
    return;
  }
  if (hit->kind != Bind_kind::runtime) {
    return;
  }
  const auto& lambda = capture_frames_[*hit->innermost_crossed].lambda_name;
  const auto  inside = in_lambda(capture_frames_[*hit->innermost_crossed]);
  if (hit->frame == 0) {
    // The file scope has no inputs: its runtime bindings are a `mut` (or a
    // `reg`/`wire`), a variable whose value changes over the file.
    report_error(
        at,
        "nested-runtime-capture",
        "name",
        std::format("'{}' is not a compile-time constant a nested lambda can recompute; it is not visible inside {}", name, inside),
        std::format("a nested lambda reads an enclosing value only through a `const` that folds to a compile-time "
                    "constant: bind the `mut` to one (`const k = {}`), or pass '{}' as an input of {}",
                    name,
                    name,
                    lambda.empty() ? std::string{"the lambda"} : std::format("'{}'", lambda)));
  }
  report_error(at,
               "nested-runtime-capture",
               "name",
               std::format("'{}' is a runtime value of the enclosing scope; it is not visible inside {}", name, inside),
               std::format("a nested lambda sees only comptime bindings (a const with a compile-time value, generics, imports, "
                           "types, lambdas); pass '{}' as an input of {}",
                           name,
                           lambda.empty() ? std::string{"the lambda"} : std::format("'{}'", lambda)));
}

bool Prp2lnast::visible_in_lambda(std::string_view name, const Capture_lookup& hit) {
  if (!binding_visible_in_lambda(*hit.binding)) {
    return false;
  }
  if (!hit.binding->fold_const || !hit.innermost_crossed) {
    return true;  // a comptime binding, or a const a test block reads directly
  }
  // A fold_const is visible iff its value folds (ruling 2026-09-28 (32)).
  for (size_t j = hit.frame + 1; j < capture_frames_.size(); ++j) {
    if (capture_frames_[j].uncapturable.contains(name)) {
      return false;
    }
  }
  const std::string                             seed(name);
  absl::flat_hash_map<std::string, std::string> uncapturable;
  std::vector<std::string>                      generics;
  (void)slice_captures(collect_capture_visible(/*include_current=*/!capture_frames_[*hit.innermost_crossed].streamed),
                       {seed},
                       {},
                       uncapturable,
                       generics);
  return !uncapturable.contains(seed);
}

void Prp2lnast::check_capture_shadow(std::string_view name, const TSNode& at) {
  const auto hit = lookup_capture(name);
  if (!hit || !visible_in_lambda(name, *hit)) {
    return;
  }
  const std::string_view what = hit->binding->fold_const ? "constant" : "comptime";
  if (!hit->innermost_crossed) {
    // No lambda in between, but a test block's local still shadows the
    // enclosing comptime binding the test sees (a file-scope `const`).
    for (size_t j = hit->frame + 1; j < capture_frames_.size(); ++j) {
      if (capture_frames_[j].test_scope) {
        report_error(
            at,
            "variable-shadowing",
            "name",
            std::format("variable shadowing: '{}' is already declared in an enclosing scope", name),
            std::format("rename the test's variable: the enclosing {} '{}' is visible inside {}",
                        what,
                        name,
                        capture_frames_[j].lambda_name.empty() ? std::string{"the test"}
                                                               : std::format("test '{}'", capture_frames_[j].lambda_name)));
        return;
      }
    }
    return;
  }
  const auto& lambda = capture_frames_[*hit->innermost_crossed].lambda_name;
  report_error(at,
               "variable-shadowing",
               "name",
               std::format("variable shadowing: '{}' is already declared in an enclosing scope", name),
               std::format("rename the inner variable: the enclosing {} '{}' is visible inside {}",
                           what,
                           name,
                           lambda.empty() ? std::string{"the lambda"} : std::format("lambda '{}'", lambda)));
}

void Prp2lnast::check_signature_shadow(std::string_view name, const TSNode& at, std::string_view role) {
  if (name.empty() || name == "self" || capture_frames_.size() < 2) {
    return;
  }
  const auto& scope  = capture_frames_.back();
  const auto& lambda = scope.lambda_name;
  // Lexical: the innermost enclosing declaration of `name` decides; a runtime
  // binding there (an enclosing mod's input) is not visible inside the lambda,
  // and it already hides any outer same-name one.
  for (size_t i = capture_frames_.size() - 1; i-- > 0;) {
    const auto&      f = capture_frames_[i];
    std::string_view what;
    if (const auto it = f.names.find(name); it != f.names.end()) {
      std::optional<size_t> crossed;
      for (size_t j = capture_frames_.size(); j-- > i + 1 && !crossed;) {
        if (capture_frames_[j].lambda_boundary) {
          crossed = j;
        }
      }
      const Capture_lookup hit{.kind = it->second.kind, .frame = i, .innermost_crossed = crossed, .binding = &it->second};
      if (!visible_in_lambda(name, hit)) {
        return;
      }
      what = it->second.fold_const ? "constant" : "comptime";
    } else if (f.types.contains(name)) {
      what = "type";
    } else if (f.lambdas.contains(name)) {
      // A lambda does not enclose itself: `comb parity(d:u8) -> (parity:u1)`
      // (the Verilog idiom of a port named like its module) names a port.
      if (i + 2 == capture_frames_.size() && scope.lambda_boundary && canonical_escaped_ident(lambda) == name) {
        continue;
      }
      what = "lambda";
    } else {
      continue;
    }
    report_error(at,
                 "variable-shadowing",
                 "name",
                 std::format("variable shadowing: '{}' is already declared in an enclosing scope", name),
                 std::format("rename the {}: the enclosing {} '{}' is visible inside {}",
                             role,
                             what,
                             name,
                             lambda.empty()     ? std::string{"the lambda"}
                             : scope.test_scope ? std::format("test '{}'", lambda)
                                                : std::format("lambda '{}'", lambda)));
  }
}

bool Prp2lnast::ufcs_writes_receiver(std::string_view callee) const {
  if (ref_param_funcs_.contains(callee)) {
    return true;
  }
  for (const auto& f : capture_frames_) {
    if (f.lambdas.contains(callee)) {
      return false;
    }
  }
  return !streamed_function_names_.contains(callee);
}

void Prp2lnast::note_call_write(std::string_view name) {
  if (auto* b = find_binding(name)) {
    b->int_value.reset();
  }
}

void Prp2lnast::note_implicit_generic(const Capture_lookup& hit, std::string_view name) {
  for (size_t j = hit.frame + 1; j < capture_frames_.size(); ++j) {
    auto& f = capture_frames_[j];
    if (f.lambda_boundary && f.streamed
        && std::find(f.implicit_generics.begin(), f.implicit_generics.end(), name) == f.implicit_generics.end()) {
      f.implicit_generics.emplace_back(name);
    }
  }
}

void Prp2lnast::note_capture_read(std::string_view name, const TSNode& at) {
  if (const auto hit = lookup_capture(name); hit && hit->innermost_crossed && hit->binding->fold_const) {
    resolve_kept_lambda_fold_const(name, *hit);
  }
  check_capture_read(name, at);
  if (const auto hit = lookup_capture(name); hit && hit->innermost_crossed && hit->kind == Bind_kind::generic) {
    note_implicit_generic(*hit, name);
  }
}

void Prp2lnast::note_comptime_rvalue(const Lnast_node& tmp) {
  if (tmp.is_ref()) {
    comptime_rvalue_tmps_.emplace(lnast.get(), std::string(tmp.get_name()));
  }
}

bool Prp2lnast::is_comptime_rvalue(const Lnast_node& value) const {
  if (!value.is_ref()) {
    return false;
  }
  const std::string name(value.get_name());
  // A named lambda bound to a new name (`const f = add1`) is a lambda too.
  return streamed_function_names_.contains(name)
         || comptime_rvalue_tmps_.contains(std::pair<const Lnast*, std::string>{lnast.get(), name});
}

// Every name `e` reads is a comptime binding (a comptime const, a plain const
// with a comptime value, a generic, a loop index, an import, a type, a lambda)
// or a runtime one whose value is statically known (Binding::int_value), and
// every call is a type cast, a `std` function or a `comb` (resolved
// lexically): a `mod`/`pipe` call is an instance, a runtime value. An
// attribute read (`a.[bits]`) is comptime whatever it reads (ruling
// 2026-09-28 #24); a nested lambda that cannot recompute one reports it (see
// Capture_frame::uncapturable). A name with no binding is a type, a lambda, a
// builtin, or an undefined name that check_undefined_reads reports. A lambda
// literal is comptime only as the whole value (`const f = comb(…){…}`): a
// capture cannot replay a method hoisted out of a tuple literal.
bool Prp2lnast::is_comptime_expression(TSNode e) const {
  const auto reads = expr_reads(e, /*enter_attribute_reads=*/false);
  for (TSNode c : reads.names) {
    const auto hit = lookup_capture(canonical_escaped_ident(trim(get_text(c))));
    if (hit && hit->kind == Bind_kind::runtime && !hit->binding->int_value) {
      return false;
    }
  }
  for (TSNode fn : reads.callees) {
    const std::string_view ft(ts_node_type(fn));
    bool                   comptime = false;
    if (ft == "identifier") {
      const auto name = trim(get_text(fn));
      comptime        = prp_builtins::is_type_cast_callee(name) || resolves_to_comb(canonical_escaped_ident(name));
    } else if (ft == "dot_expression" && ts_node_named_child_count(fn) == 2) {
      comptime = trim(get_text(ts_node_named_child(fn, 0))) == prp_builtins::std_namespace
                 && prp_builtins::is_std_member(trim(get_text(ts_node_named_child(fn, 1))));
    }
    if (!comptime) {
      return false;
    }
  }
  for (TSNode l : reads.lambdas) {
    if (ts_node_start_byte(l) != ts_node_start_byte(e) || ts_node_end_byte(l) != ts_node_end_byte(e)) {
      return false;
    }
  }
  return true;
}

bool Prp2lnast::resolves_to_comb(std::string_view name) const {
  for (size_t i = capture_frames_.size(); i-- > 0;) {
    if (const auto it = capture_frames_[i].lambdas.find(name); it != capture_frames_[i].lambdas.end()) {
      return it->second;
    }
  }
  return false;
}

void Prp2lnast::check_lambda_used_as_value(TSNode operand) {
  if (ts_node_is_null(operand) || std::string_view(ts_node_type(operand)) != "identifier") {
    return;
  }
  const auto name = canonical_escaped_ident(trim(get_text(operand)));
  for (size_t i = capture_frames_.size(); i-- > 0;) {
    if (capture_frames_[i].names.contains(name)) {
      return;  // a variable of that name hides the lambda
    }
    if (capture_frames_[i].lambdas.contains(name)) {
      report_error(operand,
                   "lambda-needs-call",
                   "syntax",
                   std::format("`{}` is a lambda: every lambda call requires parentheses, write `{}()`", name, name),
                   "call it with `name()` / `name(arg=...)`; a lambda cannot be used as an operand value");
    }
  }
}

bool Prp2lnast::callee_is_replayable(std::string_view callee) const {
  if (resolves_to_comb(callee) || prp_builtins::is_type_cast_callee(callee)) {
    return true;
  }
  if (callee.starts_with(prp_builtins::std_namespace) && callee.size() > prp_builtins::std_namespace.size()
      && callee[prp_builtins::std_namespace.size()] == '.'
      && prp_builtins::is_std_member(callee.substr(prp_builtins::std_namespace.size() + 1))) {
    return true;
  }
  const auto hit = lookup_capture(callee);
  return hit && hit->kind == Bind_kind::comptime;  // a lambda value (`const f = comb(…){…}`)
}

// Plan the capture prologue of a streamed lambda (see capture_frames_). Runs
// once the lambda's boundary frame is pushed (so every enclosing binding reads
// as crossed) and its destination opened (so every enclosing tree sits in
// destination_stack_ with its builder cursor at the declaration point).
Prp2lnast::Capture_plan Prp2lnast::plan_streamed_captures(TSNode lambda_node, TSNode code) {
  Capture_plan plan;
  plan.rename.resize(destination_stack_.size());
  // The lambda's own generics, inputs and outputs: a read of one of them is
  // never a capture (the signature is not lowered yet, so the boundary frame
  // does not hold them).
  absl::flat_hash_set<std::string> own;
  for (TSNode fdef : ts_node_named_children(lambda_node)) {
    if (std::string_view(ts_node_type(fdef)) != "function_definition_decl") {
      continue;
    }
    const auto add_own = [&](TSNode item) {
      const std::string_view it(ts_node_type(item));
      TSNode id = it == "typed_identifier" ? child_by_field(item, "identifier") : (it == "identifier" ? item : TSNode{});
      if (!ts_node_is_null(id)) {
        own.insert(std::string(canonical_escaped_ident(trim(get_text(id)))));
      }
    };
    for (uint32_t i = 0; i < child_count(fdef); ++i) {
      const char* fn = ts_node_field_name_for_child(fdef, i);
      if (fn == nullptr) {
        continue;
      }
      const std::string_view field(fn);
      TSNode                 ci = child(fdef, i);
      const std::string_view ct(ts_node_type(ci));
      if (field != "generic" && field != "input" && field != "output") {
        continue;
      }
      if (ct == "typed_identifier_list" || ct == "arg_list") {
        // Only the declared names: an `arg_list` also holds `= default`
        // expressions, which may well read a capture.
        for (TSNode item : ts_node_named_children(ci)) {
          if (std::string_view(ts_node_type(item)) == "typed_identifier") {
            add_own(item);
          }
        }
      } else {
        add_own(ci);
      }
    }
  }
  // Names the lambda, its signature and any lambda nested in it may read. An
  // over-approximation is harmless: an unused capture is dead code.
  absl::flat_hash_set<std::string> ids;
  std::vector<TSNode>              todo{lambda_node};
  if (!ts_node_is_null(code)) {
    todo.push_back(code);
  }
  while (!todo.empty()) {
    TSNode c = todo.back();
    todo.pop_back();
    const std::string_view ct(ts_node_type(c));
    if (ct == "identifier") {
      ids.insert(std::string(canonical_escaped_ident(trim(get_text(c)))));
      continue;
    }
    if (ct == "dot_expression" || ct == "member_selection") {
      // Only the base names a binding; the rest are field names.
      if (ts_node_named_child_count(c) != 0) {
        todo.push_back(ts_node_named_child(c, 0));
      }
      continue;
    }
    TSNode key{};
    if (ct == "arg_assignment" || ct == "attribute_assignment") {
      key = child_by_field(c, "lvalue");  // a named argument / attribute key
    }
    for (TSNode k : ts_node_named_children(c)) {
      if (ts_node_is_null(key) || ts_node_start_byte(k) != ts_node_start_byte(key)) {
        todo.push_back(k);
      }
    }
  }

  for (const auto& id : ids) {
    if (own.contains(id)) {
      continue;
    }
    const auto hit = lookup_capture(id);
    if (!hit || !hit->innermost_crossed) {
      continue;
    }
    if (hit->kind == Bind_kind::generic) {
      note_implicit_generic(*hit, id);
    } else if (binding_visible_in_lambda(*hit->binding) && !capture_import_bindings_.contains(id)) {
      plan.seeds.push_back(id);
    }
  }
  if (plan.seeds.empty()) {
    return plan;
  }
  std::sort(plan.seeds.begin(), plan.seeds.end());

  const auto                       vis          = collect_capture_visible(/*include_current=*/false);
  auto&                            uncapturable = capture_frames_.back().uncapturable;
  std::vector<std::string>         generics;
  auto                             take = slice_captures(vis, plan.seeds, {}, uncapturable, generics);
  // A seed whose computation reads a runtime name of the enclosing scope, but
  // whose value this front end already folded to an integer (an untyped
  // comptime const: `const W = a.[bits]` of an input `a:u8`), is bound to
  // that value instead (ruling 2026-09-28 (32), fold-based). The replay would
  // read the runtime name, or the lambda's own same-name binding (`a`).
  // Every other seed is replayed, which keeps its declared type (`const t = w`
  // of a `w:u3` is a u3, ruling 38).
  absl::flat_hash_set<std::string> by_value;
  for (const auto& seed : plan.seeds) {
    const auto hit = uncapturable.contains(seed) ? lookup_capture(seed) : std::nullopt;
    if (hit && hit->kind == Bind_kind::comptime && !hit->binding->typed && hit->binding->int_value) {
      plan.values.emplace_back(seed, *hit->binding->int_value);
      by_value.insert(seed);
      uncapturable.erase(seed);
    }
  }
  if (!by_value.empty()) {
    // Re-slice the others: one computed from a bound seed replays now.
    std::vector<std::string> replayed;
    for (const auto& n : plan.seeds) {
      if (!by_value.contains(n)) {
        uncapturable.erase(n);
        replayed.push_back(n);
      }
    }
    take = slice_captures(vis, replayed, by_value, uncapturable, generics);
  }
  for (const auto& name : generics) {
    note_implicit_generic(*lookup_capture(name), name);
  }
  std::erase_if(plan.seeds, [&](const std::string& seed) { return uncapturable.contains(seed); });
  for (size_t i = 0; i < vis.stmts.size(); ++i) {
    if (!take[i]) {
      continue;
    }
    const auto depth = vis.stmts[i].depth;
    plan.stmts.push_back(Capture_copy{.src = destination_stack_[depth].lnast, .nid = vis.stmts[i].nid, .depth = depth});
    // The copy runs in the lambda's own namespace: temps and runtime names the
    // enclosing computation writes (a file-scope `mut` accumulator, a loop
    // index) get private names; the names the lambda sees keep theirs.
    auto& rename = plan.rename[depth];
    for (const auto& w : vis.rws[i]->writes) {
      if (rename.contains(w)) {
        continue;
      }
      if (Lnast::is_tmp(w)) {
        rename.emplace(w, std::format("%cap{}_{}", depth, std::string_view(w).substr(1)));
      } else if (const auto hit = lookup_capture(w); !hit || !binding_visible_in_lambda(*hit->binding)) {
        rename.emplace(w, std::format("__cap{}_{}", depth, w));
      }
    }
  }
  return plan;
}

Prp2lnast::Capture_visible Prp2lnast::collect_capture_visible(bool include_current) {
  Capture_visible vis;
  const auto      add_tree
      = [&](const Lnast& ln, const Lnast_nid& cursor, absl::node_hash_map<Lnast_nid, Prp_stmt_rw>& cache, size_t depth) {
          std::vector<Lnast_nid> nids;
          prp_visible_stmts(ln, cursor, nids);
          for (const auto& nid : nids) {
            auto [it, fresh] = cache.try_emplace(nid);
            auto& rw         = it->second;
            if (fresh) {
              if (Lnast_ntype::is_func_def(ln.get_type(nid))) {
                rw.opaque = true;
              } else if (const auto tn = prp_declared_type_name(ln, nid)) {
                rw.opaque        = true;
                rw.declared_type = *tn;
              } else {
                prp_stmt_rw(ln, nid, rw, [this](std::string_view callee) { return ufcs_writes_receiver(callee); });
              }
            }
            if (!rw.declared_type.empty()) {
              vis.type_names.insert(rw.declared_type);
            }
            vis.stmts.push_back(Capture_visible::Stmt{.nid = nid, .depth = depth});
            vis.rws.push_back(rw.opaque ? nullptr : &rw);
          }
        };
  for (size_t d = 0; d < destination_stack_.size(); ++d) {
    auto& st = destination_stack_[d];
    add_tree(*st.lnast, st.builder.idx_stmts, st.capture_rw, d);
  }
  if (include_current) {
    add_tree(*lnast, builder.idx_stmts, capture_rw_, destination_stack_.size());
  }
  return vis;
}

// Backward slice over the statements visible at the declaration point: keep
// every statement that writes a needed name, and need what it reads (and
// whatever else it writes, so that name's declaration comes along too).
std::vector<bool> Prp2lnast::slice_captures(const Capture_visible& vis, const std::vector<std::string>& seeds,
                                            const absl::flat_hash_set<std::string>&        by_value,
                                            absl::flat_hash_map<std::string, std::string>& uncapturable,
                                            std::vector<std::string>&                      generics) const {
  const auto provided = [&](const std::string& name) {
    return by_value.contains(name) || vis.type_names.contains(name) || capture_import_bindings_.contains(name)
           || streamed_function_names_.contains(name);
  };
  // A seed computed from an enclosing RUNTIME value (`comptime const K =
  // a.[bits]` with `a` an input of the enclosing mod, a `const s = r` of a
  // register) cannot be replayed: the copy would read the lambda's own
  // same-name binding, or nothing, or build a second register. Such a seed is
  // left out (a read of it reports, see check_capture_read) and the slice
  // recomputed without it.
  std::vector<bool> take;
  for (;;) {
    absl::flat_hash_map<std::string, std::string> needed;  // name -> the seed whose value needs it
    for (const auto& seed : seeds) {
      if (!uncapturable.contains(seed)) {
        needed.emplace(seed, seed);
      }
    }
    take.assign(vis.stmts.size(), false);
    size_t                                        depth = std::numeric_limits<size_t>::max();
    absl::flat_hash_map<std::string, std::string> calls;  // callee -> the seed whose value needs the call
    for (size_t i = vis.stmts.size(); i-- > 0;) {
      if (vis.stmts[i].depth != depth) {
        // Temps are numbered per tree: an inner tree's temp never names a value
        // of an enclosing tree.
        absl::erase_if(needed, [](const auto& kv) { return Lnast::is_tmp(kv.first); });
        depth = vis.stmts[i].depth;
      }
      const auto* rw = vis.rws[i];
      if (rw == nullptr) {
        continue;
      }
      std::string from;
      for (const auto& w : rw->writes) {
        if (const auto it = needed.find(w); it != needed.end() && !provided(w)) {
          from = it->second;
          break;
        }
      }
      if (from.empty()) {
        continue;
      }
      take[i] = true;
      for (const auto* names : {&rw->writes, &rw->reads}) {
        for (const auto& n : *names) {
          if (!provided(n)) {
            needed.try_emplace(n, from);
          }
        }
      }
      for (const auto& c : rw->callees) {
        calls.try_emplace(c, from);
      }
      // An earlier writer of a name declared here writes another binding (an
      // enclosing scope's, or the source a prologue copy came from).
      for (const auto& d : rw->declares) {
        needed.erase(d);
      }
    }
    absl::flat_hash_set<std::string> written;
    for (size_t i = 0; i < vis.stmts.size(); ++i) {
      if (take[i]) {
        written.insert(vis.rws[i]->writes.begin(), vis.rws[i]->writes.end());
      }
    }
    // What the copies read and do not compute themselves: an enclosing generic
    // (`comptime const K = W + 1` in `mod m<W>`) is bound per specialization;
    // an enclosing runtime value -- or a runtime binding the copies would
    // re-create (an output, a `reg`, a `wire`) -- makes its seed uncapturable.
    generics.clear();
    bool changed = false;
    for (const auto& [name, from] : needed) {
      if (Lnast::is_tmp(name) || provided(name)) {
        continue;
      }
      const auto hit = lookup_capture(name);
      if (!hit || !hit->innermost_crossed) {
        continue;
      }
      if (hit->kind == Bind_kind::generic) {
        if (!written.contains(name)) {
          generics.push_back(name);
        }
      } else if (hit->kind == Bind_kind::runtime && (!hit->binding->replayable || !written.contains(name))) {
        changed |= uncapturable.try_emplace(from, name).second;
      }
    }
    // A `mod`/`pipe` call is an instance, a runtime value: replaying it would
    // build a second instance inside the lambda. (A `comptime const` states
    // its value is comptime; its calls are left to the runner's fold.)
    for (const auto& [callee, from] : calls) {
      const auto seed = lookup_capture(from);
      if (seed && seed->binding->fold_const && !callee_is_replayable(callee)) {
        changed |= uncapturable.try_emplace(from, callee).second;
      }
    }
    if (!changed) {
      break;
    }
  }
  return take;
}

void Prp2lnast::resolve_kept_lambda_fold_const(std::string_view name, const Capture_lookup& hit) {
  auto& f = capture_frames_[*hit.innermost_crossed];
  if (f.streamed || f.fold_decided.contains(name)) {
    return;  // a streamed lambda planned its captures up front
  }
  f.fold_decided.emplace(name);
  const std::string                             seed(name);
  absl::flat_hash_map<std::string, std::string> uncapturable;
  std::vector<std::string>                      generics;
  (void)slice_captures(collect_capture_visible(/*include_current=*/true), {seed}, {}, uncapturable, generics);
  if (const auto it = uncapturable.find(seed); it != uncapturable.end()) {
    f.uncapturable.insert_or_assign(seed, it->second);
  }
}

void Prp2lnast::emit_capture_plan(const Capture_plan& plan, const Lnast_nid& body_idx) {
  for (const auto& [name, value] : plan.values) {
    auto decl = lnast->add_child(body_idx, Lnast_ntype::create_attr_set());
    lnast->add_child(decl, Lnast_node::create_ref(name));
    lnast->add_child(decl, Lnast_node::create_const("type"));
    lnast->add_child(decl, Lnast_node::create_const("const"));
    auto st = lnast->add_child(body_idx, Lnast_ntype::create_store());
    lnast->add_child(st, Lnast_node::create_ref(name));
    lnast->add_child(st, Lnast_node::create_const(Dlop::create_integer(value)->to_pyrope()));
  }
  for (const auto& c : plan.stmts) {
    // The enclosing scope already checked the value is comptime; replay the
    // declaration as a plain `const` (a replayed `comptime` attribute made a
    // `mut` initialized from it inherit the const binding in upass).
    if (const auto& ln = *c.src; Lnast_ntype::is_attr_set(ln.get_type(c.nid))) {
      const auto key = ln.get_sibling_next(ln.get_first_child(c.nid));
      if (!key.is_invalid() && ln.get_name(key) == "comptime") {
        continue;
      }
    }
    prp_copy_capture(*c.src, c.nid, *lnast, body_idx, plan.rename[c.depth], /*is_key=*/false);
  }
  streamed_scope_names_.insert(plan.seeds.begin(), plan.seeds.end());
}

void Prp2lnast::process_lambda_statement(TSNode n) { process_lambda_statement_named(n, {}); }

void Prp2lnast::reject_std_declaration(TSNode name, std::string_view what) {
  if (ts_node_is_null(name) || canonical_escaped_ident(trim(get_text(name))) != prp_builtins::std_namespace) {
    return;
  }
  // A user `mod std` next to the built-in namespace would split `std(a=x)` (the
  // user's) from `std.clog2(x)` (the built-in) with no diagnostic.
  report_error(name,
               "std-reserved",
               "name",
               std::format("the {} name `std` is reserved: `std` is the built-in namespace", what),
               "rename it: `std` is the built-in namespace, visible in every file without an import");
}

void Prp2lnast::process_lambda_statement_named(TSNode n, std::string_view hoist_name) {
  TSNode func_type_node = child_by_field(n, "func_type");
  TSNode name_node      = child_by_field(n, "name");
  reject_std_declaration(name_node, "lambda");
  // function_definition_decl is a named child that isn't directly a field
  TSNode fdef;
  for (TSNode c : ts_node_named_children(n)) {
    std::string_view t(ts_node_type(c));
    if (t == "function_definition_decl") {
      fdef = c;
      break;
    }
  }
  TSNode code = child_by_field(n, "code");

  // Record a method declared with a `ref self` parameter (the implicit UFCS
  // receiver). The `mod` field of an `arg_list` item is the `ref` token; only
  // `ref self` makes a call illegal in a rhs — an explicit `ref x` arg (the
  // caller writes `f(ref x)`) is the caller opting into the mutation, so a
  // result bind there is fine. Used by process_assignment / find_ref_param_call.
  {
    std::string fname = !ts_node_is_null(name_node) ? std::string(trim(get_text(name_node))) : std::string(hoist_name);
    if (!fname.empty() && !ts_node_is_null(fdef)) {
      if (TSNode inp = child_by_field(fdef, "input"); !ts_node_is_null(inp)) {
        for (uint32_t i = 0, nc = ts_node_child_count(inp); i < nc; ++i) {
          const char* fn = ts_node_field_name_for_child(inp, i);
          if (!fn || std::string_view(fn) != "mod" || trim(get_text(ts_node_child(inp, i))) != "ref") {
            continue;
          }
          // The typed_identifier following the `ref` mod is the ref param; only a
          // `self` receiver triggers the rhs ban.
          for (uint32_t j = i + 1; j < nc; ++j) {
            TSNode c = ts_node_child(inp, j);
            if (std::string_view(ts_node_type(c)) != "typed_identifier") {
              continue;
            }
            TSNode id = child_by_field(c, "identifier");
            if (!ts_node_is_null(id) && trim(get_text(id)) == "self") {
              ref_param_funcs_.insert(fname);
            }
            break;
          }
          break;
        }
      }
    }
  }

  std::string_view kind       = ts_node_is_null(func_type_node) ? std::string_view{"comb"} : trim(get_text(func_type_node));
  // A pipe lambda's depth attribute (`pipe[3]`, `pipe[2..=5]`,
  // `pipe[1..<4]`, bare `pipe`) resolves to a per-output stages(min,max)
  // annotation stamped on the io list below; the kind const stays the plain
  // string "pipe". The grammar exposes the depth as the pipe_lambda node's
  // `depth` field (a `select`).
  bool             is_pipe    = kind.size() >= 4 && kind.substr(0, 4) == "pipe";
  int64_t          stages_min = 0;
  int64_t          stages_max = 0;
  if (is_pipe) {
    std::tie(stages_min, stages_max) = parse_pipe_depth(func_type_node);
    kind                             = "pipe";
  }

  // `::[lg="name"]` — explicit lgraph/module-name override (todo/pyrope/2f-lg).
  // The attribute rides the function_definition_decl's `pipe_config` slot
  // (grammar `_attr_prefix` = `:: attribute_sq`); it can only appear here, on a
  // lambda name. Rules: pub-only, value is a comptime string, non-sticky (it
  // lives on this one func_def, never propagated). The validated name lands on
  // the pub entry below; func_extract copies it onto the extracted Lnast and
  // tolg renames the artifact (NOT the `import("file.entity")` key). A bare/
  // misplaced `lg` on a non-lambda is rejected by reject_common_mistakes_attr_name.
  std::string lg_value;
  bool        has_lg        = false;
  bool        timecheck_off = false;  // `::[timecheck=false]` (old spelling `::[hdl]`): timing checks off
  TSNode      lg_node{};
  if (!ts_node_is_null(fdef)) {
    // The `::[…]` prefix lands in the function_definition_decl's `pipe_config`
    // field. Its grammar rule `_attr_prefix` is hidden (`:: attribute_sq`), so
    // the field tags BOTH the `::` token and the attribute_sq node — pick the
    // attribute_sq, not the bare token (child_by_field would return the `::`).
    for (uint32_t ci = 0; ci < ts_node_child_count(fdef); ci++) {
      TSNode      pc = ts_node_child(fdef, ci);
      const char* fn = ts_node_field_name_for_child(fdef, ci);
      if (!fn || std::string_view(fn) != "pipe_config") {
        continue;
      }
      std::string_view pct(ts_node_type(pc));
      if (pct != "attribute_sq" && pct != "tuple_sq") {
        continue;  // the `::` punctuation, not the bracket list
      }
      for (TSNode item : ts_node_named_children(pc)) {
        std::string_view it(ts_node_type(item));
        std::string_view key;
        TSNode           rv{};
        if (it == "attribute_assignment") {
          TSNode lv = child_by_field(item, "lvalue");
          rv        = child_by_field(item, "rvalue");
          if (!ts_node_is_null(lv)) {
            key = trim(get_text(lv));
          }
        } else if (it == "identifier" || it == "ref_identifier") {
          key = trim(get_text(item));
        }
        if (key == "timecheck") {
          // `timecheck=false` opts a lambda OUT of the Pyrope timing / comb-cycle
          // checks: a plain reg is an always_ff cycle-0 STATE element (not a
          // feedforward pipeline stage), an undriven wire / unresolved comb-cycle
          // net reads as X (not a hard error), and a same-cycle ring through a
          // wire is not flagged as a combinational loop. It relaxes TIMING only
          // (user ruling 2026-09-28 (22)): every other Pyrope rule still holds.
          // prp_writer emits it on Pyrope regenerated from Verilog (which
          // routinely carries these firtool/handshake shapes). `timecheck=true`
          // (the default) keeps all checks.
          const std::string_view val = ts_node_is_null(rv) ? std::string_view{} : trim(get_text(rv));
          if (val == "false" || val == "0") {
            timecheck_off = true;
          } else if (!val.empty() && val != "true" && val != "1") {
            report_error(item,
                         "timecheck-bad-value",
                         "type",
                         "the `timecheck` attribute must be `false` (opt out of the timing/comb-loop checks) or `true`",
                         "write `timecheck=false`");
          }
          continue;
        }
        if (key == "hdl") {
          timecheck_off = true;  // DEPRECATED spelling of `timecheck=false` (same effect)
          continue;
        }
        if (key != "lg") {
          continue;  // other lambda-name attributes are out of scope here
        }
        has_lg  = true;
        lg_node = item;
        if (auto sv = plain_string_literal_text(rv); sv) {
          lg_value = *sv;
        } else {
          report_error(item,
                       "lg-not-string",
                       "type",
                       "the `lg` attribute value must be a compile-time string literal (e.g. `lg=\"chip_top\"`)",
                       "pin the module name with a quoted string constant");
        }
      }
    }
  }

  // `pub comb/mod/pipe/fluid name …` marks the definition
  // exportable. Only file-scope named definitions may be pub.
  TSNode     pub_node = child_by_field(n, "pub");
  const bool is_pub   = !ts_node_is_null(pub_node);
  if (is_pub) {
    if (!builder.at_top_stmts() || !lambda_kind_stack_.empty() || conditional_depth_ > 0) {
      report_error(pub_node,
                   "pub-not-file-scope",
                   "syntax",
                   "`pub` is only valid on file-scope definitions",
                   "move the definition to the file's top level");
    }
    if (ts_node_is_null(name_node)) {
      report_error(pub_node,
                   "pub-needs-name",
                   "syntax",
                   "`pub` requires a named definition",
                   "anonymous lambdas cannot be exported");
    }
    // Normalize the fluid kind text ("fluid …" variants) to the bare kind.
    std::string_view pub_kind = kind.size() >= 5 && kind.substr(0, 5) == "fluid" ? "fluid" : kind;
    lnast->add_pub(canonical_escaped_ident(trim(get_text(name_node))), pub_kind, mint_src(name_node), lg_value);
  }
  // The `lg` rename is pub-only: a non-pub (file-local) unit has no stable
  // export name to pin. Diagnose here rather than silently dropping it.
  if (has_lg && !is_pub) {
    report_error(lg_node,
                 "lg-requires-pub",
                 "type",
                 "the `lg` attribute is only valid on a `pub` lambda definition",
                 "add `pub` before the definition, or remove the `lg` attribute");
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

  // 06-functions.md "Output tuple": outputs are always declared explicitly —
  // `-> (out, …)`, or `-> ()` for none. A missing `->` clause is a compile
  // error, never an implicit "no outputs": the old last-expression /
  // implicit-return sugar is gone, so an undeclared-output lambda can only be
  // a mistake (its caller has nothing to bind — see named_tuple.prp history).
  // Exemption: `self` methods (first input param named `self`, `ref` or not)
  // act through the receiver — setters, ctors, or debug-only prints/asserts —
  // and may omit the clause.
  if (!ts_node_is_null(fdef)) {
    bool has_output_clause = false;
    for (uint32_t i = 0; i < child_count(fdef); i++) {
      const char* fname = ts_node_field_name_for_child(fdef, i);
      if (fname && std::string_view(fname) == "output") {
        has_output_clause = true;  // the `->` anchor token carries the field tag
        break;
      }
    }
    if (!has_output_clause) {
      bool   first_param_is_self = false;
      TSNode inp                 = child_by_field(fdef, "input");
      if (!ts_node_is_null(inp)) {
        for (uint32_t i = 0; i < child_count(inp); i++) {
          TSNode ci = child(inp, i);
          if (std::string_view(ts_node_type(ci)) != "typed_identifier") {
            continue;
          }
          TSNode id           = child_by_field(ci, "identifier");
          first_param_is_self = !ts_node_is_null(id) && trim(get_text(id)) == "self";
          break;  // only the FIRST param can be the receiver
        }
      }
      if (!first_param_is_self) {
        // Prefer the SOURCE name; hoisted in-tuple methods fall back to the
        // mangled hoist name only when anonymous.
        std::string lname(!ts_node_is_null(name_node) ? trim(get_text(name_node))
                          : !hoist_name.empty()       ? hoist_name
                                                      : std::string_view{"lambda"});
        report_error(n,
                     "lambda-missing-output",
                     "syntax",
                     std::format("`{}` declares no outputs: the `-> (...)` output list is missing", lname),
                     "declare the outputs explicitly; use `-> ()` for a lambda with no outputs");
      }
    }
  }

  // A lambda may not take a name the call site intercepts before it ever looks
  // for a lambda (`concat`, `import`, the assert family — see
  // prp_builtins::is_reserved_lambda_name). Without this the definition parses,
  // every call is rewritten into the built-in, and the body is silently dead.
  // Only the SOURCE-named top-level shape is rejected: a hoisted in-tuple method
  // (`t.concat = comb(…)`) is reached through a receiver, which is never
  // intercepted.
  if (hoist_name.empty() && !ts_node_is_null(name_node)) {
    const auto src_name = trim(get_text(name_node));
    if (prp_builtins::is_reserved_lambda_name(src_name)) {
      report_error(name_node,
                   "lambda-reserved-name",
                   "name",
                   std::format("`{}` is a built-in — a {} may not take that name", src_name, kind),
                   std::format("rename the {} (`{}_impl`), or make it a method (`x.{}(…)` is not a built-in call)",
                               kind,
                               src_name,
                               src_name));
    }
  }

  // canonical_escaped_ident like every other ident route: a module whose name
  // collides with a Pyrope keyword is WRITTEN back escaped (`` `pipe` ``, see
  // prp_writer's quote_module_path), and the backticks are lexical armor, not
  // part of the name. Reading them into the func_def ref verbatim produced a
  // ref text `` `pipe` `` that pass.lnastfmt rejects ("a backtick-escaped
  // pure-alnum name means the producer didn't normalize it"), so the writer's
  // own output would not round-trip.
  Lnast_node lambda_ref = !hoist_name.empty()          ? Lnast_node::create_ref(hoist_name)
                          : ts_node_is_null(name_node) ? builder.mint_tmp_ref()
                                                       : Lnast_node::create_ref(canonical_escaped_ident(trim(get_text(name_node))));

  // Named hardware lambdas stream directly into their final sibling tree.
  // Leave only a compact declaration marker in the enclosing destination: it
  // preserves hoisting and source-unit semantic hashing, while pass.upass drops
  // it without materializing/copying the body. Anonymous/fluid/test lambdas
  // keep the legacy func_def representation.
  // A tuple method is lowered while tuple_to_node is still collecting the
  // enclosing literal.  Keep that hoisted method as an ordinary func_def in
  // the current destination: switching destinations here would make the
  // method body re-enter tuple lowering before the enclosing tuple has been
  // completed.  File/nested source definitions (which have no synthetic
  // hoist_name) can stream directly into their final sibling tree.
  const bool stream_lambda = hoist_name.empty() && lambda_ref.is_ref() && !lambda_ref.get_name().empty()
                             && (kind == "comb" || kind == "pipe" || kind == "mod");
  absl::flat_hash_set<std::string> outer_hoisted_names;
  if (stream_lambda) {
    // Types, enums, and sibling function names are file/function-scope
    // comptime declarations.  The streamed child no longer has an ancestor
    // func_def node through which read_is_visible can discover them, so carry
    // the compact name set across the destination switch.  Their definitions
    // remain in the enclosing source-unit tree and are consumed through the
    // shared runner registries; no declaration subtree is copied. Every
    // enclosing tree contributes: a lambda nested in a `mod` still sees the
    // file's types.
    collect_hoisted_names(*lnast, lnast->get_root(), outer_hoisted_names);
    for (const auto& st : destination_stack_) {
      collect_hoisted_names(*st.lnast, st.lnast->get_root(), outer_hoisted_names);
    }
  }
  std::string streamed_entity;
  Lnast_nid   fd_idx;
  Lnast_nid   signature_parent;
  if (lambda_ref.is_ref() && !ts_node_is_null(name_node)) {
    capture_frames_.back().lambdas.insert_or_assign(std::string(lambda_ref.get_name()), kind == "comb");
  }
  if (stream_lambda) {
    const std::string entity(lambda_ref.get_name());
    streamed_entity = entity;
    streamed_function_names_.insert(entity);
    if (destination_stack_.empty()) {
      auto marker = builder.add_child(Lnast_ntype::create_func_def());
      lnast->add_child(marker, lambda_ref);
      lnast->add_child(marker, Lnast_node::create_const(std::format("__streamed_{}", kind)));
      // The source-unit wrapper needs a compact body fingerprint for semantic
      // cache equality.  A nested definition needs no second marker: its own
      // streamed sidecar is attached to this same source unit, and the outer
      // wrapper's fingerprint already covers the complete nested source text.
      // Comment/whitespace-insensitive on purpose: a comment edit inside a
      // body must stay a comment-only warm hit (see stable_source_fingerprint).
      lnast->add_child(marker, Lnast_node::create_const(std::to_string(stable_source_fingerprint(get_text(n)))));
      attach_loc(marker, n);
    }

    const std::string streamed_name = std::format("{}.{}", lnast->get_top_module_name(), entity);
    push_streamed_destination(streamed_name,
                              kind,
                              timecheck_off || lnast->get_skip_timecheck(),
                              is_pub ? lg_value : std::string_view{});
    // The streamed destination has no enclosing func_def node from which the
    // source mapper can inherit the declaration span.  Seed its root so that
    // synthesized io/declaration nodes still map back to this function.
    attach_loc(lnast->get_root(), n);
    streamed_scope_names_.insert(outer_hoisted_names.begin(), outer_hoisted_names.end());
    streamed_scope_names_.insert(streamed_function_names_.begin(), streamed_function_names_.end());
    signature_parent = lnast->add_child(lnast->get_root(), Lnast_ntype::create_io());
  } else {
    fd_idx = builder.add_child(Lnast_ntype::create_func_def());
    lnast->add_child(fd_idx, lambda_ref);
    auto kind_idx = lnast->add_child(fd_idx, Lnast_node::create_const(kind));
    // `timecheck=false` belongs to THIS lambda. A streamed lambda carries it on
    // its own destination (push_streamed_destination above); a func_def kept in
    // the enclosing tree carries it on its kind node, and func_extract stamps
    // only the extracted lambda. Marking the enclosing tree would switch the
    // checks off for everything else in it.
    if (timecheck_off) {
      lnast->add_child(kind_idx, Lnast_node::create_const(lambda_timecheck_false_marker));
    }
    signature_parent = fd_idx;
    // An anonymous lambda is a comptime value: `const f = comb(…){…}` binds a
    // lambda, which a nested lambda may read.
    if (ts_node_is_null(name_node) && hoist_name.empty()) {
      note_comptime_rvalue(lambda_ref);
    }
  }
  // The lambda's capture scope: its generics, inputs and outputs, and the line
  // every enclosing binding crosses (see capture_frames_). A streamed lambda
  // plans its comptime-capture prologue now -- it may make the lambda generic
  // before the signature lowers -- and emits it once the body stmts exist.
  const size_t        boundary_frame = capture_frames_.size();
  Capture_frame_guard boundary_guard(
      capture_frames_,
      Capture_frame{.lambda_boundary = true,
                    .streamed        = stream_lambda,
                    .lambda_name = !ts_node_is_null(name_node) ? std::string(trim(get_text(name_node))) : std::string(hoist_name)});
  const Capture_plan       capture_plan = stream_lambda ? plan_streamed_captures(n, code) : Capture_plan{};
  // generics tuple: each `<T, U>` name becomes a `ref` child of
  // this tuple_add. Empty when absent. func_extract copies these onto the
  // extracted Lnast (Lnast::generics_) so a generic signature is detected as a
  // template; the per-`T` body substitution is a deferred follow-up goal.
  // grammar.js: `function_definition_decl` carries the names under the
  // `generic` field as a `typed_identifier_list` between `<` and `>`.
  Lnast_nid                gen_idx;
  std::vector<std::string> streamed_generics;
  std::vector<std::string> streamed_generic_defaults;
  if (!stream_lambda) {
    gen_idx = lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  }
  // In-flight signature frame: the generics, then the params and outputs as
  // collect_args flushes them, are readable by a later generic default, port
  // bound or default-value expression (which lower into the ENCLOSING stmts
  // frame, or none for a streamed lambda). Popped before the body — body reads
  // resolve against the func_def signature in read_is_visible instead.
  inflight_name_scopes_.emplace_back();
  if (!ts_node_is_null(fdef)) {
    uint32_t fcount = child_count(fdef);
    for (uint32_t i = 0; i < fcount; i++) {
      TSNode      ci = child(fdef, i);
      const char* fn = ts_node_field_name_for_child(fdef, i);
      if (!fn || std::string_view(fn) != "generic") {
        continue;
      }
      std::string_view ct(ts_node_type(ci));
      auto             add_one = [&](TSNode ti) {
        TSNode id = child_by_field(ti, "identifier");
        // User ruling 2026-09-28 (19): a generic is macro substitution with no
        // constraint clause (docs 06-functions), so `<W:u8>` is an error, never
        // a silently dropped type.
        if (TSNode gt = child_by_field(ti, "type"); !ts_node_is_null(id) && !ts_node_is_null(gt)) {
          const auto gname = trim(get_text(id));
          auto       gtype = trim(get_text(gt));
          if (gtype.starts_with(':')) {
            gtype = trim(gtype.substr(1));
          }
          report_error(gt,
                       "generic-typed-param",
                       "type",
                       std::format("generic parameter `{}` cannot declare a type (`{}:{}`): a generic is substituted as "
                                               "written, with no constraint clause",
                                   gname,
                                   gname,
                                   gtype),
                       std::format("drop `:{}` and check the bound value in the body: `cassert({} does {})`", gtype, gname, gtype));
        }
        if (!ts_node_is_null(id)) {
          Lnast_nid gref;
          check_signature_shadow(canonical_escaped_ident(trim(get_text(id))), id, "generic");
          note_binding(canonical_escaped_ident(trim(get_text(id))), Bind_kind::generic);
          // A default is read by name at each call (the runner re-reads it),
          // so a name it reads must be declared here: this generic and the
          // later ones are not yet visible to it.
          if (TSNode def = child_by_field(ti, "definition"); !ts_node_is_null(def)) {
            if (!lookup_capture(canonical_escaped_ident(trim(get_text(def))))) {
              check_type_name_spelling(def);  // `<T=boolean>`, unless a value is named so
            }
            record_name_reads(def, Generic_read::default_value);
          }
          inflight_name_scopes_.back().emplace_back(get_text(id));
          if (stream_lambda) {
            streamed_generics.emplace_back(get_text(id));
            streamed_scope_names_.insert(std::string(get_text(id)));
          } else {
            gref = lnast->add_child(gen_idx, Lnast_node::create_ref(get_text(id)));
          }
          // A DECLARATION default (`<T, N=1>`, todo 3g B) rides as the default's
          // source text in a `const` child of the generic ref: func_extract
          // lifts it into Lnast::generic_defaults_, and the runner re-classifies
          // it (type / constant / lambda) exactly like an explicit `<…>` arg.
          TSNode      def = child_by_field(ti, "definition");
          std::string default_text;
          if (!ts_node_is_null(def)) {
            // Cooked strings use double quotes in source, but LNAST/Dlop
            // constants carry the decoded, single-quoted representation.
            // Preserve the same value as an ordinary string expression.
            auto literal = def;
            while (std::string_view(ts_node_type(literal)) == "expression_type" && ts_node_named_child_count(literal) == 1) {
              literal = ts_node_named_child(literal, 0);
            }
            if (const auto value = plain_string_literal_text(literal); value) {
              default_text = Dlop::from_string(*value)->to_pyrope();
            } else if (const auto folded
                       = std::string_view(ts_node_type(literal)) == "identifier" ? std::nullopt : resolve_type_int_value(def);
                       folded) {
              // A comptime integer written as an expression (`<N=Z.[bits]>`,
              // `<N=(Z.[bits])>`, `<N=(W*2)>`): fold it HERE, in the scope of
              // the declaration. The runner applies a default at each call
              // site, where a name the default reads may not be visible. A
              // bare name (`<N=W>`) stays a name: the runner binds the entity,
              // so a typed W keeps its declared `.[bits]`.
              default_text = std::string(folded->to_pyrope());
            } else if (std::string_view(ts_node_type(literal)) == "attribute_read") {
              // The runner re-reads a default as a type, a constant or a
              // lambda name; an attribute read is none of those, so one that
              // does not fold here (`Z.[bits]` of a `Z:unsigned`, which is nil)
              // must not reach it.
              report_error(def,
                           "generic-default-not-comptime",
                           "type",
                           std::format("default `{}` of generic `{}` does not fold to a compile-time integer",
                                       trim(get_text(def)),
                                       trim(get_text(id))),
                           "`.[bits]`/`.[max]`/`.[min]` of a comptime value declared without bounds (`Z:Unsigned`) "
                                       "is nil: give it a width, or write the number");
            } else {
              default_text             = std::string(trim(get_text(def)));
              // Signed numeric defaults are grouped by the grammar (`N=(-1)`).
              // Store the numeric value, not the grouping, for specialization.
              // Peel only a BALANCED outer pair: `((-1)*(3-4))`'s first '(' closes
              // mid-text, and blind peeling would hand `-1)*(3-4` to from_pyrope.
              const auto peel_balanced = [](std::string_view t) {
                while (t.size() >= 2 && t.front() == '(' && t.back() == ')') {
                  int depth = 0;
                  for (std::size_t pos = 0; pos < t.size(); ++pos) {
                    depth += t[pos] == '(' ? 1 : (t[pos] == ')' ? -1 : 0);
                    if (depth == 0 && pos + 1 != t.size()) {
                      return t;  // the leading '(' is not the one the last ')' closes
                    }
                  }
                  t = trim(t.substr(1, t.size() - 2));
                }
                return t;
              };
              const auto numeric = peel_balanced(std::string_view(default_text));
              if (numeric.size() > 1 && (numeric.front() == '-' || numeric.front() == '+')
                  && std::isdigit(static_cast<unsigned char>(numeric[1])) != 0) {
                // Best effort: from_pyrope THROWS on anything past a single signed
                // literal (`(-1+2)`, `(-4..=4)`, `(-1,2)`, `(-0b1010)`), and those
                // are all grammatically legal here. Keep the source text so the
                // runner/tolg reports its own LOCATED error, exactly as the
                // unsigned twin `(1+2)` already does -- never an `internal` class
                // exception with no span.
                try {
                  default_text = std::string(Dlop::from_pyrope(numeric)->to_pyrope());
                } catch (const std::exception&) {  // NOLINT: source text stays the diagnostic carrier
                }
              }
            }
          }
          if (stream_lambda) {
            streamed_generic_defaults.emplace_back(default_text);
          } else if (!ts_node_is_null(def)) {
            lnast->add_child(gref, Lnast_node::create_const(default_text));
          }
        }
      };
      if (ct == "typed_identifier_list") {
        for (TSNode item : ts_node_named_children(ci)) {
          if (std::string_view(ts_node_type(item)) == "typed_identifier") {
            add_one(item);
          }
        }
      } else if (ct == "typed_identifier") {
        add_one(ci);
      } else if (ct == "identifier") {
        check_signature_shadow(canonical_escaped_ident(trim(get_text(ci))), ci, "generic");
        note_binding(canonical_escaped_ident(trim(get_text(ci))), Bind_kind::generic);
        inflight_name_scopes_.back().emplace_back(get_text(ci));
        if (stream_lambda) {
          streamed_generics.emplace_back(get_text(ci));
          streamed_generic_defaults.emplace_back();
          streamed_scope_names_.insert(std::string(get_text(ci)));
        } else {
          lnast->add_child(gen_idx, Lnast_node::create_ref(get_text(ci)));
        }
      }
    }
  }
  // 2f-generic_port_width — a port bound of THIS lambda may read its generics
  // (see Pending_port_bound). Scoped to the signature: restored once the body
  // frame opens (flush_deferred_port_bounds below), so a nested lambda starts
  // clean.
  const bool saved_has_generics
      = std::exchange(lambda_has_generics_,
                      stream_lambda ? !streamed_generics.empty() || !capture_frames_[boundary_frame].implicit_generics.empty()
                                    : (!gen_idx.is_invalid() && !lnast->get_first_child(gen_idx).is_invalid()));
  if (stream_lambda) {
    lnast->set_generics(std::move(streamed_generics));
    lnast->set_generic_defaults(std::move(streamed_generic_defaults));
  }

  // Emit input args. The grammar tags `ref`/`reg`/`...` prefixes on each arg
  // via the `mod` field and `= expr` defaults via the `definition` field;
  // iterate ALL children to pair `mod` and `definition` with the typed_ident
  // they bracket. The `ref` mod is encoded as the assign's RHS const text
  // ("ref") when no explicit default is present; downstream passes
  // (func_extract, constprop) detect it without inventing a new ntype.
  auto                                        in_idx = lnast->add_child(signature_parent, Lnast_ntype::create_tuple_add());
  std::vector<Param_attr>                     input_attrs;
  // `-> (reg q:T[@N] [= init])` output registers: the q pin IS the
  // output (the counter idiom). Collected here; a matching `reg` declare is
  // synthesized at body entry below so tolg lowers the output as a Flop.
  std::vector<std::pair<TSNode, TSNode>>      output_regs;  // (typed_identifier, definition)
  // Comb INPUT defaults (`comb f(in1:u4, in2=3)`, todo 3g E): (param name,
  // default expr node), in declaration order. Lowered as a body-prologue
  // `store(name, expr)` after the body stmts open (so the default evaluates in
  // param-tuple scope — an EARLIER param is readable — and is self-contained).
  std::vector<std::pair<std::string, TSNode>> input_defaults;
  auto                                        collect_args = [&](TSNode                    container,
                          const Lnast_nid&          parent_tup,
                          std::vector<std::string>* names_out,
                          std::vector<Param_attr>*  attrs_out,
                          bool                      is_io_output) {
    TSNode pending_typed{};
    TSNode pending_def{};
    bool   pending_is_ref    = false;
    bool   pending_is_reg    = false;
    bool   pending_is_vararg = false;
    bool   next_is_ref       = false;
    bool   next_is_reg       = false;
    bool   next_is_vararg    = false;
    auto   flush             = [&]() {
      if (!ts_node_is_null(pending_typed)) {
        emit_arg_assign(parent_tup, pending_typed, pending_def, pending_is_ref, attrs_out, kind, is_io_output, pending_is_vararg);
        TSNode id = child_by_field(pending_typed, "identifier");
        if (!ts_node_is_null(id)) {
          // Earlier params are readable by LATER params' default values
          // (`comb f(a, b = a+5)` — 04-variables.md "Tuple scope" default
          // example): expose through the in-flight signature frame.
          inflight_name_scopes_.back().emplace_back(get_text(id));
          const std::string pname(canonical_escaped_ident(get_text(id)));
          check_signature_shadow(pname, id, is_io_output ? "output" : "input");
          auto&  pb = note_binding(pname, Bind_kind::runtime);
          TSNode pt = child_by_field(pending_typed, "type");
          if (!ts_node_is_null(pt)) {
            pb.typed = true;
            pb.range = folded_int_type_range(child_by_field(pt, "type"));
          }
          if (stream_lambda) {
            streamed_scope_names_.insert(pname);
          }
          if (names_out) {
            names_out->emplace_back(get_text(id));
          }
          // A comb INPUT default (and a mod/pipe one that does not fold here)
          // rides to the body prologue (todo 3g E); the io slot only carries
          // the `__default` sentinel (see emit_arg_assign).
          if (input_default_in_prologue(pending_def, kind, is_io_output, pending_is_vararg)) {
            input_defaults.emplace_back(std::string(trim(get_text(id))), pending_def);
          }
        }
        // A `reg` modifier on an OUTPUT marks an output register
        // (q is the output); on an input it is meaningless.
        if (pending_is_reg) {
          if (is_io_output) {
            output_regs.emplace_back(pending_typed, pending_def);
          } else {
            report_error(pending_typed,
                         "reg-input",
                         "type",
                         "a `reg` modifier is not valid on an input",
                         "registers hold state inside the body — declare the input plain and `reg` a body variable");
          }
        }
        // `ref` is a comb-only inline+alias mechanism: the comb is inlined and
        // the ref param becomes the caller's variable. A `mod`/`pipe` is a real
        // module boundary (not inlined), so a non-`self` `ref` parameter there
        // is a compile error. (`ref self` methods expand locally via UFCS and
        // never expose the receiver as a port.) (2f-ref_wrap_sat B.)
        if (pending_is_ref && !is_io_output && kind != "comb") {
          TSNode           rid = child_by_field(pending_typed, "identifier");
          std::string_view rname = ts_node_is_null(rid) ? std::string_view{} : get_text(rid);
          if (rname != "self") {
            report_error(pending_typed,
                         "ref-on-boundary",
                         "type",
                         std::format("a `ref` parameter is not valid on a `{}` — `ref` is comb-only (inline + alias)", kind),
                         "make the lambda a `comb`, or pass the value and return the updated copy");
          }
        }
      }
      pending_typed     = TSNode{};
      pending_def       = TSNode{};
      pending_is_ref    = false;
      pending_is_reg    = false;
      pending_is_vararg = false;
    };
    uint32_t nc = child_count(container);
    for (uint32_t i = 0; i < nc; i++) {
      TSNode           ci    = child(container, i);
      const char*      fname = ts_node_field_name_for_child(container, i);
      std::string_view ct(ts_node_type(ci));
      if (fname && std::string_view(fname) == "mod") {
        // grammar arg_list `mod` field: choice('...', 'ref', 'reg'). The marker
        // is an anonymous token, so classify by its source text (not node type).
        std::string_view mod_txt = trim(get_text(ci));
        next_is_ref              = (mod_txt == "ref");
        next_is_reg              = (mod_txt == "reg");
        next_is_vararg           = (mod_txt == "...");  // var-args param
        continue;
      }
      if (ct == "typed_identifier") {
        flush();
        pending_typed     = ci;
        pending_is_ref    = next_is_ref;
        pending_is_reg    = next_is_reg;
        pending_is_vararg = next_is_vararg;
        next_is_ref       = false;
        next_is_reg       = false;
        next_is_vararg    = false;
        continue;
      }
      if (fname && std::string_view(fname) == "definition") {
        // The `definition` field wraps `= expr`; the `=` token also carries
        // the field tag. Skip the `=` token and keep the expression.
        if (ct != "=") {
          pending_def = ci;
        }
        continue;
      }
    }
    flush();
  };
  if (!ts_node_is_null(fdef)) {
    TSNode inp = child_by_field(fdef, "input");
    if (!ts_node_is_null(inp)) {
      collect_args(inp, in_idx, nullptr, &input_attrs, /*is_io_output=*/false);
    }
  }
  if (stream_lambda && lnast->get_first_child(in_idx).is_invalid()) {
    lnast->add_child(in_idx, Lnast_node::create_ref("__empty_tuple"));
  }

  // Output args
  auto                     out_idx = lnast->add_child(signature_parent, Lnast_ntype::create_tuple_add());
  std::vector<std::string> output_refs;  // for placeholder-lambda implicit assign
  if (!ts_node_is_null(fdef)) {
    // Collect ALL output field occurrences (there may be multiple due to "->" anchor + arg_list form)
    for (uint32_t i = 0; i < child_count(fdef); i++) {
      const char* fname = ts_node_field_name_for_child(fdef, i);
      if (!fname || std::string_view(fname) != "output") {
        continue;
      }
      TSNode           o = child(fdef, i);
      std::string_view ot(ts_node_type(o));
      if (ot == "arg_list") {
        collect_args(o, out_idx, &output_refs, nullptr, /*is_io_output=*/true);
      } else if (ot == "typed_identifier") {
        emit_arg_assign(out_idx, o, TSNode{}, /*is_ref_mod=*/false, nullptr, kind, /*is_io_output=*/true);
        TSNode id = child_by_field(o, "identifier");
        if (!ts_node_is_null(id)) {
          output_refs.emplace_back(get_text(id));
          check_signature_shadow(canonical_escaped_ident(get_text(id)), id, "output");
          note_binding(canonical_escaped_ident(get_text(id)), Bind_kind::runtime);
        }
      }
    }
  }
  if (stream_lambda && lnast->get_first_child(out_idx).is_invalid()) {
    lnast->add_child(out_idx, Lnast_node::create_ref("__empty_tuple"));
  }

  inflight_name_scopes_.pop_back();  // signature frame ends here

  // Stamp stages(min,max) as the TRAILING child of every OUTPUT io
  // store entry (after the optional type subtree; downstream identifies it by
  // ntype, never by position). Inputs never carry the annotation.
  if (is_pipe) {
    for (auto c = lnast->get_first_child(out_idx); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (!Lnast_ntype::is_store(lnast->get_type(c))) {
        continue;
      }
      auto st = lnast->add_child(c, Lnast_ntype::create_stages());
      lnast->add_child(st, Lnast_node::create_const(std::to_string(stages_min)));
      lnast->add_child(st, Lnast_node::create_const(std::to_string(stages_max)));
    }
  }

  // Body. A lambda body does NOT see the enclosing in-flight constructs — a
  // method hoisted out of a tuple literal must not resolve bare names against
  // the literal's fields (runtime upper-scope names need `self` or explicit
  // args; 04-variables.md "Lambda scope"). Suspend the stack; params resolve
  // via the func_def signature in read_is_visible. The stack comes back on
  // every exit: a compile error thrown in the body unwinds through the
  // enclosing tuple literal's Inflight_scope_guard, which pops its frame.
  struct Inflight_suspend {
    std::vector<std::vector<std::string>>& scopes;
    std::vector<std::vector<std::string>>  saved;
    explicit Inflight_suspend(std::vector<std::vector<std::string>>& s) : scopes(s), saved(std::exchange(s, {})) {}
    ~Inflight_suspend() { scopes = std::move(saved); }
    Inflight_suspend(const Inflight_suspend&)            = delete;
    Inflight_suspend& operator=(const Inflight_suspend&) = delete;
  };
  std::optional<Inflight_suspend> suspended_inflight(std::in_place, inflight_name_scopes_);
  auto body_idx = lnast->add_child(stream_lambda ? lnast->get_root() : fd_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);
  if (!ts_node_is_null(fdef)) {
    for (TSNode pc : ts_node_named_children(fdef)) {
      if (std::string_view(ts_node_type(pc)) == "attribute_sq") {
        emit_synth_scope(pc, 1);
      }
    }
  }
  if (stream_lambda) {
    // Captures are emitted before the first user statement (see
    // capture_frames_). File-scope imports come first: the import map is an
    // absl hash map, so iterate it SORTED to keep the prologue (and every hash
    // derived from it downstream, the compile cache's semantic hash above all)
    // process-independent.
    std::vector<std::pair<std::string_view, std::string_view>> sorted_imports(capture_import_bindings_.begin(),
                                                                              capture_import_bindings_.end());
    std::sort(sorted_imports.begin(), sorted_imports.end());
    for (const auto& [name, unit] : sorted_imports) {
      if (streamed_scope_names_.contains(name) || (!name.empty() && name.front() == '%')) {
        continue;
      }
      auto fc = lnast->add_child(body_idx, Lnast_ntype::create_func_call());
      lnast->add_child(fc, Lnast_node::create_ref(name));
      lnast->add_child(fc, Lnast_node::create_const("import"));
      lnast->add_child(fc, Lnast_node::create_const(unit));
      streamed_scope_names_.insert(std::string(name));
    }
    // The enclosing scopes' enums replay in SOURCE DECLARATION order. Enums
    // are comptime bundles, not scalar constants; re-lowering the retained
    // declaration gives a body read such as `Color.Green` a real local producer
    // both when the comb is inlined and when it is compiled as its own module.
    // Before the captured consts, which may read one (`const START = St.Idle`).
    std::vector<TSNode> enum_decls;
    for (size_t f = 0; f < boundary_frame; ++f) {
      enum_decls.insert(enum_decls.end(), capture_frames_[f].enum_decls.begin(), capture_frames_[f].enum_decls.end());
    }
    replaying_capture_enum_ = true;
    for (const auto& decl : enum_decls) {
      process_enum_assignment(decl);
    }
    replaying_capture_enum_ = false;
    // The comptime consts the lambda reads, computed as the enclosing scopes
    // compute them.
    emit_capture_plan(capture_plan, body_idx);
  }
  // Comb INPUT defaults (todo 3g E): the FIRST body statements, in declaration
  // order, so the default evaluates in param-tuple scope (an earlier param is
  // already visible) and is self-contained (survives func_extract). The value
  // lands in the Lnast_io_entry::default_value_name() local, never in the port:
  // the comb's own module reads its input port (the default local is dead
  // there), and only the inliner, at a call that OMITS the arg, binds the param
  // from it; a PROVIDED arg skips the store. An expression `b = a + 5` emits its
  // tmp here too (not in the enclosing frame). Emitted before the reg/attr
  // prologues below and the user body.
  //
  // 2f-generic_port_width — port bounds deferred by emit_arg_assign: lower
  // their desugar into the body prologue (the frame is body_idx now) and point
  // the io leaves at the results. Before the input defaults and the body, so a
  // lambda nested in the body starts with a clean generic flag.
  flush_deferred_port_bounds();
  lambda_has_generics_ = saved_has_generics;
  for (const auto& [pname, pdef] : input_defaults) {
    // Lower the default FIRST (it may append a `%t = a + 5` tmp to the body),
    // then the store that consumes it — so the tmp precedes its use.
    auto       val   = expr_to_node(pdef);
    const auto local = Lnast_io_entry::default_value_name(canonical_escaped_ident(pname));
    auto       stix  = lnast->add_child(body_idx, Lnast_ntype::create_store());
    lnast->add_child(stix, Lnast_node::create_ref(local));
    lnast->add_child(stix, val);
    if (stream_lambda) {
      streamed_scope_names_.insert(local);
    }
  }
  // Synthesize the body `reg` declare for each `-> (reg q:T)` output
  // register, the same cluster process_lvalue_for_assign emits for a body
  // `reg q:T = init` (the decl-merge folds it into declare(q, T, reg,
  // [init])). The q pin is the output; body stores are its din writes.
  for (const auto& [or_typed, or_def] : output_regs) {
    if (kind == "comb") {
      report_error(or_typed,
                   "reg-in-comb",
                   "type",
                   "a `comb` may not declare a `reg` (combinational logic holds no state)",
                   "move the register into a `pipe`/`mod` body, or pass the value through an output");
    }
    TSNode or_id = child_by_field(or_typed, "identifier");
    if (ts_node_is_null(or_id)) {
      continue;
    }
    auto or_ref = Lnast_node::create_ref(get_text(or_id));
    auto setix  = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(setix, or_ref);
    lnast->add_child(setix, Lnast_node::create_const("type"));
    lnast->add_child(setix, Lnast_node::create_const("reg"));
    attach_loc(setix, or_id);
    if (!ts_node_is_null(or_def)) {
      // A reset value is a compile-time constant: the same value the io slot
      // folded (a constant-valued `const RST` is its value, never the text).
      lnast->add_child(setix, comptime_default_node(or_def, or_id));
    }
    TSNode or_tc = child_by_field(or_typed, "type");
    if (!ts_node_is_null(or_tc)) {
      emit_type_spec(or_ref, or_tc);
    }
  }
  // Parameter-side attribute carriers (`a::[comptime]`, …) are sugar for an
  // `attr_set(a, key, val)` + a `cassert(a.[key])` at body entry. The attr_set
  // marks the local symbol so reads inside the body fold; for `comptime` the
  // cassert enforces the constraint once function-call inlining substitutes
  // the actual argument.
  for (const auto& pa : input_attrs) {
    auto sref  = Lnast_node::create_ref(pa.param);
    auto setix = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(setix, sref);
    lnast->add_child(setix, Lnast_node::create_const(pa.key));
    lnast->add_child(setix, Lnast_node::create_const(pa.value.empty() ? std::string{"true"} : pa.value));
    if (pa.key == "comptime") {
      auto tmp = builder.mint_tmp_ref();
      auto gix = builder.add_child(Lnast_ntype::create_attr_get());
      lnast->add_child(gix, tmp);
      lnast->add_child(gix, sref);
      lnast->add_child(gix, Lnast_node::create_const("comptime"));
      auto cix = builder.add_child(Lnast_ntype::create_cassert());
      lnast->add_child(cix, tmp);
    }
  }
  // Body-processing context: gates `stage[N]` (mod-only) and
  // `x@[N]` timecheck emission (mod/pipe only).
  lambda_kind_stack_.emplace_back(kind);

  if (!ts_node_is_null(code)) {
    // Placeholder lambda (06-functions.md "Output tuple"): when the body of
    // a single-output `comb` is a bare expression, the value is implicitly
    // assigned to the single output. Detect: scope_statement has exactly
    // one named child and that child is an expression-as-statement node.
    //
    // `if_expression` / `match_expression` are dual-use: they parse as
    // expressions but the body branches often contain plain statements that
    // explicitly write to the output (`if cond { res = a + 1 } else { … }`).
    // For those, the placeholder treatment would inject a synthetic tmp
    // (`assign output = ___1`) that overrides the in-branch writes and the
    // function ends up returning the tmp's default of 0. So only treat
    // if/match as placeholder when no branch contains an `assignment` to
    // (or other statement-level write into) the named outputs.
    bool   placeholder = false;
    TSNode expr_body{};
    if (kind == "comb" && output_refs.size() == 1 && std::string_view(ts_node_type(code)) == "scope_statement"
        && ts_node_named_child_count(code) == 1) {
      TSNode           only = ts_node_named_child(code, 0);
      std::string_view ot(ts_node_type(only));
      if (ot == "expression_item" || ot == "constant" || ot == "identifier" || ot == "unary_expression"
          || ot == "function_call_expression" || ot == "tuple" || ot == "tuple_sq" || ot == "bit_selection"
          || ot == "member_selection" || ot == "attribute_read" || ot == "dot_expression") {
        placeholder = true;
        expr_body   = only;
      } else if (ot == "if_expression" || ot == "match_expression") {
        // Recursively walk the if/match: if any descendant is an `assignment`
        // (any flavor) or a `control_statement`/`while`/`for`/`loop`, the
        // body is statement-style and the placeholder treatment is wrong.
        std::function<bool(TSNode)> has_stmt = [&](TSNode node) -> bool {
          if (ts_node_is_null(node)) {
            return false;
          }
          std::string_view tt(ts_node_type(node));
          if (tt == "assignment" || tt == "declaration_statement" || tt == "control_statement" || tt == "while_statement"
              || tt == "for_statement" || tt == "loop_statement") {
            return true;
          }
          for (TSNode sc : ts_node_named_children(node)) {
            if (has_stmt(sc)) {
              return true;
            }
          }
          return false;
        };
        if (!has_stmt(only)) {
          placeholder = true;
          expr_body   = only;
        }
      }
    }
    if (placeholder) {
      Lnast_node val  = expr_to_node(expr_body);
      auto       aidx = builder.add_child(Lnast_ntype::create_store());
      lnast->add_child(aidx, Lnast_node::create_ref(output_refs.front()));
      lnast->add_child(aidx, val);
    } else {
      // Early-return desugar (2f-return_leak): a body with a `return` inside a
      // loop gets a synthesized `mut <flag> = false` at its top; returns then
      // set the flag (and `break`), and post-loop continuations guard on it.
      // Straight-line / `if` returns still use the clean no-flag scope-rewrite.
      // Save/restore so a nested lambda gets its own flag + fresh in-loop state.
      std::string saved_ret_flag = return_flag_name_;
      bool        saved_in_loop  = in_return_loop_;
      return_flag_name_.clear();
      in_return_loop_ = false;
      if (ts_subtree_has_loop_return(code, false)) {
        // NOT a `___`-tmp name: it is a real loop-carried `mut` var (written in
        // the loop, read after), and the inline frame must rename it as a var
        // (inl-tag), not rename_tmp it into a single-assignment SSA temp.
        return_flag_name_ = "__retflag" + std::to_string(synth_return_flag_count_++);
        auto didx         = builder.add_child(Lnast_ntype::create_declare());
        lnast->add_child(didx, Lnast_node::create_ref(return_flag_name_));
        lnast->add_child(didx, Lnast_ntype::create_prim_type_bool());
        lnast->add_child(didx, Lnast_node::create_const("mut"));
        auto sidx = builder.add_child(Lnast_ntype::create_store());
        lnast->add_child(sidx, Lnast_node::create_ref(return_flag_name_));
        lnast->add_child(sidx, Lnast_node::create_const("false"));
      }
      process_scope_statement(code, body_idx);
      return_flag_name_ = saved_ret_flag;
      in_return_loop_   = saved_in_loop;
    }
  }
  if (!lambda_kind_stack_.empty()) {
    lambda_kind_stack_.pop_back();
  }
  builder.pop_stmts();
  suspended_inflight.reset();
  if (stream_lambda) {
    // Enclosing generics (and loop indices) the lambda reads become its
    // IMPLICIT generics, bound `<G=G>` by every call in the enclosing scope, so
    // each specialization of the enclosing lambda binds its own value.
    const auto implicit = capture_frames_[boundary_frame].implicit_generics;  // a copy: patching may grow the frame
    if (!implicit.empty()) {
      auto gens = lnast->get_generics();
      auto defs = lnast->get_generic_defaults();
      defs.resize(gens.size());
      for (const auto& g : implicit) {
        if (std::find(gens.begin(), gens.end(), g) == gens.end()) {
          gens.push_back(g);
          defs.emplace_back();
        }
        streamed_scope_names_.insert(g);
      }
      lnast->set_generics(std::move(gens));
      lnast->set_generic_defaults(std::move(defs));
      // Keyed by the DEFINING scope (the enclosing destination): sibling
      // scopes may each declare a same-named helper with different captures.
      I(!destination_stack_.empty());
      streamed_capture_actuals_[streamed_actuals_key(destination_stack_.back().lnast->get_top_module_name(), streamed_entity)]
          = implicit;
      patch_streamed_capture_calls(lnast, streamed_entity, implicit);
    }

    // An untyped/open-array input or generic is a deferred template, matching the former
    // extractor's metadata decision. SSA consumes the io node directly.
    if (lnast->has_generics()) {
      lnast->set_template(true);
    } else {
      for (auto entry : lnast->children(in_idx)) {
        if (!Lnast_ntype::is_store(lnast->get_type(entry))) {
          continue;
        }
        auto name_n     = lnast->get_first_child(entry);
        auto def_n      = name_n.is_invalid() ? name_n : lnast->get_sibling_next(name_n);
        auto type_n     = def_n.is_invalid() ? def_n : lnast->get_sibling_next(def_n);
        bool open_array = false;
        for (auto lvl = type_n; !lvl.is_invalid() && Lnast_ntype::is_comp_type_array(lnast->get_type(lvl));) {
          const auto elem  = lnast->get_first_child(lvl);
          const auto dim   = elem.is_invalid() ? elem : lnast->get_sibling_next(elem);
          open_array      |= dim.is_invalid() || lnast->get_name(dim) == "[]";
          lvl              = elem;
        }
        if (!name_n.is_invalid() && lnast->get_name(name_n) != "self"
            && (type_n.is_invalid() || open_array
                || (Lnast_ntype::is_const(lnast->get_type(def_n)) && lnast->get_name(def_n) == "..."))) {
          lnast->set_template(true);
          break;
        }
      }
    }
    auto completed = pop_streamed_destination();
    if (!implicit.empty()) {
      patch_streamed_capture_calls(lnast, streamed_entity, implicit);
    }
    root_lnast_->add_streamed_lambda(std::move(completed));
  }
}

// Resolve a timing-index node to a compile-time integer. A
// `constant` CST node parses through the Pyrope literal parser (so hex/octal/
// underscore-grouped forms work); an `identifier` reads the recorded integer of
// the binding it names (see Binding::int_value). Anything
// else (a mut/input/runtime ref, a non-integer const, a too-wide literal) is
// std::nullopt so the caller can emit its own diagnostic.
std::optional<int64_t> Prp2lnast::resolve_cycle_value(TSNode n) const {
  if (ts_node_is_null(n)) {
    return std::nullopt;
  }
  std::string_view t(ts_node_type(n));
  if (t == "constant") {
    auto txt = trim(get_text(n));
    if (txt.empty()) {
      return std::nullopt;
    }
    if (auto cst = Dlop::from_pyrope(txt); cst && cst->is_integer() && cst->is_just_i64()) {
      return cst->to_just_i64();
    }
    return std::nullopt;
  }
  if (t == "identifier") {
    return visible_const_int(n);
  }
  return std::nullopt;
}

std::optional<std::string> Prp2lnast::unfolded_comptime_read(TSNode n) const {
  std::vector<TSNode> todo{n};
  while (!todo.empty()) {
    TSNode c = todo.back();
    todo.pop_back();
    if (ts_node_is_null(c)) {
      continue;
    }
    const std::string_view t(ts_node_type(c));
    if (t == "identifier") {
      const auto name = canonical_escaped_ident(trim(get_text(c)));
      if (const auto hit = lookup_capture(name); hit && hit->kind == Bind_kind::comptime && !hit->binding->int_value) {
        return std::string(name);
      }
      continue;
    }
    if (t == "dot_expression" || t == "member_selection" || (t == "expression_type" && ts_node_named_child_count(c) > 1)) {
      continue;  // a field of an import or a comptime tuple, not a comptime integer
    }
    for (TSNode k : ts_node_named_children(c)) {
      todo.push_back(k);
    }
  }
  return std::nullopt;
}

std::optional<int64_t> Prp2lnast::visible_const_int(TSNode id) const {
  const auto nm = canonical_escaped_ident(trim(get_text(id)));
  check_capture_read(nm, id);
  const auto hit = lookup_capture(nm);
  return hit ? hit->binding->int_value : std::nullopt;
}

// Resolve the pipe_lambda `depth` field to the (min,max) stages
// pair (see hpp). 06c-pipelining.md admits `pipe[N]`, `pipe[A..=B]`,
// `pipe[A..<B]` and bare `pipe` — open-ended ranges (`pipe[2..]`) and
// non-literal depths are rejected.
std::pair<int64_t, int64_t> Prp2lnast::parse_pipe_depth(TSNode pipe_lambda_node) {
  TSNode depth = child_by_field(pipe_lambda_node, "depth");
  if (ts_node_is_null(depth)) {
    return {1, 0};  // bare `pipe`: min 1 (contract: N >= 1), max 0 = unconstrained
  }

  // A `constant` literal goes through the Pyrope literal parser (so hex/octal/
  // underscore-grouped forms like `0x3` / `1_0` work), or a compile-time-
  // resolvable `const NAME` is looked up — the same helper as the @[N] /
  // stage[N] timing slots.
  auto lit_int = [&](TSNode c) -> std::optional<int64_t> { return resolve_cycle_value(c); };

  // Depths ride int32 fields downstream (Lnast_io_entry::stages_min/max);
  // diagnose anything beyond instead of silently truncating.
  constexpr int64_t k_max_depth = std::numeric_limits<int32_t>::max();
  auto              check_max   = [&](int64_t v, TSNode anchor) {
    if (v > k_max_depth) {
      report_error(anchor,
                   "invalid-pipe-depth",
                   "type",
                   std::format("invalid pipe depth {}: exceeds the maximum of {}", v, k_max_depth),
                   "use a realistic stage count");
    }
  };

  TSNode index_n = child_by_field(depth, "index");

  // `pipe[N]` — a single integer literal.
  if (auto v = lit_int(index_n)) {
    if (*v < 1) {
      report_error(index_n,
                   "invalid-pipe-depth",
                   "type",
                   std::format("invalid pipe depth {}: a pipe has at least one stage", *v),
                   "use `pipe[1]` for a single-stage pipe, or `comb` for pure combinational logic");
    }
    check_max(*v, index_n);
    return {*v, *v};
  }

  // `pipe[A..=B]` / `pipe[A..<B]` — closed literal ranges parse as an
  // expression_item (const, binary_other_op(op_range_inclusive|exclusive),
  // const). The select's `range` field only carries open-ended forms, which
  // are not valid pipe depths.
  if (!ts_node_is_null(index_n) && std::string_view(ts_node_type(index_n)) == "expression_item"
      && ts_node_named_child_count(index_n) == 3) {
    TSNode lhs = ts_node_named_child(index_n, 0);
    TSNode op  = ts_node_named_child(index_n, 1);
    TSNode rhs = ts_node_named_child(index_n, 2);
    if (std::string_view(ts_node_type(op)) == "binary_other_op") {
      TSNode op_inner = ts_node_named_child(op, 0);
      if (!ts_node_is_null(op_inner)) {
        std::string_view op_kind(ts_node_type(op_inner));
        bool             inclusive = op_kind == "op_range_inclusive";
        if (inclusive || op_kind == "op_range_exclusive") {
          auto lo = lit_int(lhs);
          auto hi = lit_int(rhs);
          if (lo && hi) {
            if (*lo < 1) {
              report_error(lhs,
                           "invalid-pipe-depth",
                           "type",
                           std::format("invalid pipe depth range minimum {}: a pipe has at least one stage", *lo),
                           "start the range at 1 or higher, e.g. `pipe[1..=4]`");
            }
            int64_t max = inclusive ? *hi : *hi - 1;
            if (max < *lo) {
              report_error(
                  op,
                  "invalid-pipe-depth",
                  "type",
                  std::format("invalid pipe depth range: {} never reaches {} (only ascending ranges are allowed)", *lo, *hi),
                  "swap the bounds so the range ascends, e.g. `pipe[2..=5]`");
            }
            check_max(max, rhs);
            return {*lo, max};
          }
        }
      }
    }
  }

  report_error(depth,
               "invalid-pipe-depth",
               "type",
               "pipe depth must be a literal integer (or a compile-time constant) or a literal ascending range",
               "write `pipe[3]`, `pipe[2..=5]`, `pipe[1..<4]`, or bare `pipe`");
}

// stage_decl timing slot → (min,max) const texts (see hpp).
std::pair<std::string, std::string> Prp2lnast::parse_stage_slot(TSNode storage_node) {
  TSNode slot{};
  if (!ts_node_is_null(storage_node)) {
    for (TSNode c : ts_node_named_children(storage_node)) {
      if (std::string_view(ts_node_type(c)) == "timing_slot") {
        slot = c;
        break;
      }
    }
  }
  // Bare `stage` / `stage[]` — count unconstrained, the toolchain picks.
  if (ts_node_is_null(slot)) {
    return {"nil", "nil"};
  }
  TSNode index_n = child_by_field(slot, "index");
  if (ts_node_is_null(index_n)) {
    if (TSNode range_n = child_by_field(slot, "range"); !ts_node_is_null(range_n)) {
      report_error(range_n,
                   "invalid-stage-count",
                   "type",
                   "stage count must be a literal integer or a literal ascending closed range",
                   "write `stage[3]`, `stage[2..=5]`, or `stage[]` to let the toolchain pick");
    }
    return {"nil", "nil"};
  }

  // `constant` literal or a compile-time-resolvable `const NAME`.
  auto lit_int = [&](TSNode c) -> std::optional<int64_t> { return resolve_cycle_value(c); };

  // `stage[N]` — a single literal.
  if (auto v = lit_int(index_n)) {
    if (*v < 1) {
      report_error(index_n,
                   "invalid-stage-count",
                   "type",
                   std::format("invalid stage count {}: a stage delivers its value at least one cycle later", *v),
                   "use `stage[1]`, or a plain assignment for a same-cycle value");
    }
    auto txt = std::to_string(*v);
    return {txt, txt};
  }

  // `stage[A..=B]` / `stage[A..<B]` — closed literal ranges (expression_item).
  if (std::string_view(ts_node_type(index_n)) == "expression_item" && ts_node_named_child_count(index_n) == 3) {
    TSNode lhs = ts_node_named_child(index_n, 0);
    TSNode op  = ts_node_named_child(index_n, 1);
    TSNode rhs = ts_node_named_child(index_n, 2);
    if (std::string_view(ts_node_type(op)) == "binary_other_op") {
      TSNode op_inner = ts_node_named_child(op, 0);
      if (!ts_node_is_null(op_inner)) {
        std::string_view op_kind(ts_node_type(op_inner));
        bool             inclusive = op_kind == "op_range_inclusive";
        if (inclusive || op_kind == "op_range_exclusive") {
          auto lo = lit_int(lhs);
          auto hi = lit_int(rhs);
          if (lo && hi) {
            if (*lo < 1) {
              report_error(lhs,
                           "invalid-stage-count",
                           "type",
                           std::format("invalid stage count range minimum {}: a stage delivers at least one cycle later", *lo),
                           "start the range at 1 or higher, e.g. `stage[1..=4]`");
            }
            int64_t max = inclusive ? *hi : *hi - 1;
            if (max < *lo) {
              report_error(
                  op,
                  "invalid-stage-count",
                  "type",
                  std::format("invalid stage count range: {} never reaches {} (only ascending ranges are allowed)", *lo, *hi),
                  "swap the bounds so the range ascends, e.g. `stage[2..=5]`");
            }
            return {std::to_string(*lo), std::to_string(max)};
          }
        }
      }
    }
  }

  report_error(index_n,
               "invalid-stage-count",
               "type",
               "stage count must be a literal integer (or a compile-time constant) or a literal ascending closed range",
               "write `stage[3]`, `stage[2..=5]`, or `stage[]` to let the toolchain pick");
}

// `x@[N]` → inert timecheck statement (see hpp).
void Prp2lnast::maybe_emit_timecheck(TSNode timing_slot, TSNode id_node) {
  if (ts_node_is_null(timing_slot)) {
    return;
  }
  if (lambda_kind_stack_.empty() || lambda_kind_stack_.back() == "comb") {
    report_error(timing_slot,
                 "cycle-check-not-in-comb",
                 "type",
                 "`@[N]` cycle checks are only meaningful inside `mod` and `pipe` bodies",
                 "comb values are all at cycle 0 by definition — drop the `@[...]`");
  }
  TSNode index_n = child_by_field(timing_slot, "index");
  if (ts_node_is_null(index_n)) {
    if (TSNode range_n = child_by_field(timing_slot, "range"); !ts_node_is_null(range_n)) {
      report_error(range_n,
                   "invalid-cycle-check",
                   "type",
                   "a cycle check needs a literal cycle or a closed ascending range",
                   "write `@[N]`, `@[A..=B]` / `@[A..<B]`, or opt out with `@[]`");
    }
    return;  // `@[]` — explicit opt-out, nothing recorded
  }
  // `@[A..=B]` / `@[A..<B]` — a value reachable through paths of different
  // depths (e.g. mux arms at different cycles) lands in a cycle RANGE; the
  // check asserts the whole interval (same expression_item shape as the
  // stage[A..=B] slot).
  int64_t cmin = -1;
  int64_t cmax = -1;
  if (std::string_view(ts_node_type(index_n)) == "expression_item" && ts_node_named_child_count(index_n) == 3) {
    TSNode lhs = ts_node_named_child(index_n, 0);
    TSNode op  = ts_node_named_child(index_n, 1);
    TSNode rhs = ts_node_named_child(index_n, 2);
    if (std::string_view(ts_node_type(op)) == "binary_other_op") {
      TSNode op_inner = ts_node_named_child(op, 0);
      if (!ts_node_is_null(op_inner)) {
        std::string_view op_kind(ts_node_type(op_inner));
        bool             inclusive = op_kind == "op_range_inclusive";
        if (inclusive || op_kind == "op_range_exclusive") {
          auto lo = resolve_cycle_value(lhs);
          auto hi = resolve_cycle_value(rhs);
          if (lo && hi) {
            cmin = *lo;
            cmax = inclusive ? *hi : *hi - 1;
            if (cmin < 0 || cmax < cmin) {
              report_error(op,
                           "invalid-cycle-check",
                           "type",
                           std::format("invalid cycle-check range [{}, {}]: cycles start at 0 and only ascending ranges "
                                       "are allowed",
                                       cmin,
                                       cmax),
                           "write an ascending range like `@[1..=2]`");
            }
          }
        }
      }
    }
  }
  if (cmin < 0) {
    std::optional<int64_t> lit = resolve_cycle_value(index_n);
    if (!lit || *lit < 0) {
      report_error(index_n,
                   "invalid-cycle-check",
                   "type",
                   "a cycle check must resolve to a cycle >= 0 (a literal or compile-time constant, counted from the "
                   "enclosing lambda's inputs)",
                   "write `@[N]` / `@[A..=B]` with literals (or a `const` resolvable to one), or opt out with `@[]`");
    }
    cmin = *lit;
    cmax = *lit;
  }
  auto tcix = builder.add_child(Lnast_ntype::create_timecheck());
  lnast->add_child(tcix, identifier_to_node(id_node, /*for_lvalue=*/true));
  lnast->add_child(tcix, Lnast_node::create_const(std::to_string(cmin)));
  lnast->add_child(tcix, Lnast_node::create_const(std::to_string(cmax)));
  attach_loc(tcix, timing_slot);
}

// A nested-enum entry value can use either the explicit
// `bird = enum(eagle, parrot)` spelling or the documented bare tuple
// `bird = (,eagle, ,parrot)`. Returns the entry container, or a null TSNode
// when the value is anything else (ordinal literal, payload ctor, expression).
static TSNode enum_definition_node_of(TSNode rv) {
  if (ts_node_is_null(rv)) {
    return rv;
  }
  const std::string_view type(ts_node_type(rv));
  if (type == "enum_definition" || type == "tuple") {
    return rv;
  }
  return TSNode{};
}

void Prp2lnast::parse_enum_definition_entries(TSNode enum_def_node, std::vector<Enum_entry>& entries) {
  if (std::string_view(ts_node_type(enum_def_node)) == "tuple") {
    for (TSNode item : ts_node_named_children(enum_def_node)) {
      const std::string_view item_type(ts_node_type(item));
      if (item_type == "comment") {
        continue;
      }
      Enum_entry e;
      if (item_type == "identifier") {
        e.name = trim(get_text(item));
      } else if (item_type == "typed_identifier") {
        TSNode id = child_by_field(item, "identifier");
        e.name    = trim(get_text(ts_node_is_null(id) ? item : id));
        if (TSNode tc = child_by_field(item, "type"); !ts_node_is_null(tc)) {
          e.type_node = tc;
          e.has_type  = true;
        }
      } else if (item_type == "assignment") {
        TSNode lv = child_by_field(item, "lvalue");
        TSNode rv = child_by_field(item, "rvalue");
        if (ts_node_is_null(lv)) {
          continue;
        }
        if (std::string_view(ts_node_type(lv)) == "typed_identifier") {
          TSNode id = child_by_field(lv, "identifier");
          e.name    = trim(get_text(ts_node_is_null(id) ? lv : id));
          if (TSNode tc = child_by_field(lv, "type"); !ts_node_is_null(tc)) {
            e.type_node = tc;
            e.has_type  = true;
          }
        } else {
          e.name = trim(get_text(lv));
        }
        if (!ts_node_is_null(rv)) {
          e.value_node = rv;
          e.has_value  = true;
        }
      }
      if (!e.name.empty()) {
        entries.push_back(std::move(e));
      }
    }
    return;
  }

  TSNode args = child_by_field(enum_def_node, "input");
  if (ts_node_is_null(args)) {
    return;
  }
  // Entries ride in the `input` arg_list as lambda-style pairs: a
  // `typed_identifier` (name + optional payload type) optionally followed by a
  // `definition`-tagged value expression.
  bool pending_spread = false;  // a `...` mod token precedes the operand identifier
  for (uint32_t i = 0, nc = child_count(args); i < nc; ++i) {
    TSNode           c     = child(args, i);
    const char*      fname = ts_node_field_name_for_child(args, i);
    std::string_view ct(ts_node_type(c));
    if (ct == "comment") {
      continue;
    }
    // `enum(...NAME, …)`: a `...` mod flags the NEXT identifier as a SPREAD of a
    // comptime const — a const string splices as a field name, a const tuple
    // splices its named fields, so `enum(...a, b=3, ...c)` lowers exactly like
    // `enum("field", b=3, const foo=4)` (03-bundle.md, 2f-enum group D).
    if (fname && std::string_view(fname) == "mod" && trim(get_text(c)) == "...") {
      pending_spread = true;
      continue;
    }
    if (ct == "typed_identifier") {
      if (pending_spread) {
        pending_spread = false;
        expand_enum_spread(c, entries);
        continue;
      }
      Enum_entry e;
      TSNode     id = child_by_field(c, "identifier");
      e.name        = trim(get_text(ts_node_is_null(id) ? c : id));
      if (TSNode tc = child_by_field(c, "type"); !ts_node_is_null(tc)) {
        e.type_node = tc;
        e.has_type  = true;
      }
      entries.push_back(std::move(e));
    } else if (fname && std::string_view(fname) == "definition" && ct != "=" && !entries.empty()) {
      entries.back().value_node = c;
      entries.back().has_value  = true;
    }
  }
}

// Expand an `enum(...NAME)` spread operand (a typed_identifier naming a const)
// into enum entries: a const STRING becomes a field whose name is the string
// content; a const TUPLE splices each of its named fields (carrying their
// values). See const_rvalue_nodes_ / parse_enum_definition_entries.
void Prp2lnast::expand_enum_spread(TSNode operand, std::vector<Enum_entry>& entries) {
  TSNode      id = child_by_field(operand, "identifier");
  std::string nm(trim(get_text(ts_node_is_null(id) ? operand : id)));
  auto        it = const_rvalue_nodes_.find(nm);
  if (it == const_rvalue_nodes_.end()) {
    report_error(operand,
                 "enum-spread-unresolved",
                 "comptime",
                 std::format("enum spread `...{}` must reference a compile-time `const` string or tuple", nm),
                 "declare it as a `const` string (splices as a field name) or a `const` tuple (splices its fields)");
    return;
  }
  TSNode           rv = it->second;
  std::string_view rvt(ts_node_type(rv));
  if (rvt == "constant") {
    // String const → a field whose NAME is the string content.
    std::string s(trim(get_text(rv)));
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'')) {
      s = s.substr(1, s.size() - 2);
    }
    Enum_entry e;
    e.name = std::move(s);
    entries.push_back(std::move(e));
    return;
  }
  if (rvt == "tuple") {
    // Splice each named field (`const foo=4` → `foo=4`).
    for (TSNode item : ts_node_named_children(rv)) {
      std::string_view itt(ts_node_type(item));
      Enum_entry       e;
      if (itt == "assignment") {
        TSNode ilv = child_by_field(item, "lvalue");
        if (ts_node_is_null(ilv)) {
          continue;
        }
        if (std::string_view(ts_node_type(ilv)) == "typed_identifier") {
          TSNode iid = child_by_field(ilv, "identifier");
          e.name     = trim(get_text(ts_node_is_null(iid) ? ilv : iid));
        } else {
          e.name = trim(get_text(ilv));
        }
        if (TSNode irv = child_by_field(item, "rvalue"); !ts_node_is_null(irv)) {
          e.value_node = irv;
          e.has_value  = true;
        }
      } else if (itt == "identifier" || itt == "typed_identifier") {
        TSNode iid = (itt == "typed_identifier") ? child_by_field(item, "identifier") : item;
        e.name     = trim(get_text(ts_node_is_null(iid) ? item : iid));
      } else {
        continue;
      }
      if (!e.name.empty()) {
        entries.push_back(std::move(e));
      }
    }
    return;
  }
  report_error(operand,
               "enum-spread-unresolved",
               "comptime",
               std::format("enum spread `...{}` must be a `const` string or tuple", nm),
               "use a `const` string or a `const` named tuple");
}

Lnast_node Prp2lnast::lower_enum_def(std::string_view enum_name, TSNode enum_level_type, const std::vector<Enum_entry>& entries,
                                     TSNode loc, int64_t parent_bits, int* bit_counter, Enum_encoding* enc) {
  // An enum is a finite value hierarchy, not a recursive algebraic data
  // type. Check payload annotations before their textual carrier is stored;
  // otherwise a self-reference silently survives as an unresolved type.
  const auto                  root_name     = enum_name.substr(0, enum_name.find('.'));
  std::function<void(TSNode)> check_payload = [&](TSNode node) {
    if (ts_node_is_null(node)) {
      return;
    }
    if (std::string_view(ts_node_type(node)) == "identifier" && !root_name.empty() && trim(get_text(node)) == root_name) {
      report_error(node,
                   "recursive-enum-payload",
                   "type",
                   "recursive enum payloads are not supported",
                   "use a finite nested enum; recursive algebraic data types are not part of Pyrope");
    }
    for (TSNode child : ts_node_named_children(node)) {
      check_payload(child);
    }
  };
  for (const auto& entry : entries) {
    if (entry.has_type) {
      check_payload(entry.type_node);
    }
  }

  // Shared one-hot bit allocator: a single counter threads through the whole
  // hierarchical tree so each node gets a globally-unique bit (DFS pre-order).
  int  local_bit    = 0;
  int& bit          = bit_counter ? *bit_counter : local_bit;
  // Payload type text of a type_cast / expression_type / bare identifier.
  auto type_text_of = [&](TSNode tc) -> std::string {
    if (ts_node_is_null(tc)) {
      return {};
    }
    if (TSNode ty = child_by_field(tc, "type"); !ts_node_is_null(ty)) {
      tc = ty;
    }
    return std::string(trim(get_text(tc)));
  };
  const std::string level_pt     = type_text_of(enum_level_type);
  // An INTEGER level type (`enum V:u8`, `:unsigned(bits=4)`, an alias of one)
  // forces SEQUENTIAL ordinal numbering (0,1,2,…), not the default one-hot, and
  // makes an explicit entry value an ORDINAL (not a payload construction) that
  // resets the sequence. (2f-enum group B.)
  const bool        level_is_int = int_type_of(enum_level_type).has_value();
  const auto        one_hot_bit  = [&](std::string_view entry) {
    if (bit >= kMaxOneHotEnumBits) {
      const auto msg = std::format("one-hot enum `{}` needs more than {} bits (entry `{}`)", root_name, kMaxOneHotEnumBits, entry);
      constexpr std::string_view hint = "give the enum an integer level type (`enum E:U8 = (…)`) for sequential values";
      if (ts_node_is_null(loc)) {
        report_error("enum-too-wide", "type", msg, hint);
      }
      report_error(loc, "enum-too-wide", "type", msg, hint);
    }
    return int64_t{1} << bit++;
  };

  // Ordinal mode (payload-less, no level type): see enum_entries_sequential.
  const bool any_explicit = level_pt.empty() && enum_entries_sequential(entries);

  std::vector<std::pair<std::string, Lnast_node>> fields;
  fields.reserve(entries.size());
  Lnast_node          seq_next  = Lnast_node::create_const("0");
  std::optional<Dlop> seq_value = *Dlop::create_integer(0);  // seq_next's value, when it folds here
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto&       e    = entries[i];
    const std::string pt   = e.has_type ? type_text_of(e.type_node) : level_pt;
    const std::string full = absl::StrCat(enum_name, ".", e.name);
    Lnast_node        carrier{Lnast_node::create_invalid()};

    const TSNode nested
        = (!any_explicit && level_pt.empty() && e.has_value && !e.has_type) ? enum_definition_node_of(e.value_node) : TSNode{};
    if (!ts_node_is_null(nested)) {
      // Hierarchical entry (`bird = enum(eagle, parrot)`): the parent claims a
      // fresh bit FIRST (DFS pre-order), then its children inherit it through
      // `parent_bits`. The carrier IS the children bundle (so `.eagle` still
      // navigates); the parent's own bare-bit encoding rides as a `__enumval`
      // attr that int()/==/in read back (03-bundle.md "Hierarchical enumerates").
      const int64_t own_value = parent_bits | one_hot_bit(e.name);
      if (enc != nullptr) {
        enc->add(*Dlop::create_integer(own_value));
      }
      std::vector<Enum_entry> inner;
      parse_enum_definition_entries(nested, inner);
      // Pass the fully-qualified prefix (`E3.l1`) so nested leaf identity is
      // `E3.l1.l1a`, not the prefix-less `l1.l1a` (string() reads __enumentry).
      carrier = lower_enum_def(full, TSNode{}, inner, loc, own_value, &bit, enc);

      auto vidx = builder.add_child(Lnast_ntype::create_store());
      lnast->add_child(vidx, carrier);
      lnast->add_child(vidx, Lnast_node::create_const("__enumval"));
      lnast->add_child(vidx, Lnast_node::create_const(std::to_string(own_value)));
    } else if (!pt.empty() && e.has_value && !int_type_of(e.has_type ? e.type_node : enum_level_type)) {
      // Typed payload entry (`Yellow:Rgb = 0xff_ff00`): construct `Rgb(v)`.
      // An INTEGER-typed enum (`:i4`) is NOT a payload — its explicit value is
      // an ordinal, handled by the ordinal branch below.
      // The runner's init-construction hook splices the payload type's init.
      auto val  = expr_to_node(e.value_node);
      auto fidx = builder.add_child(Lnast_ntype::create_func_call());
      carrier   = builder.mint_tmp_ref();
      if (enc != nullptr) {
        enc->known = false;  // a tagged union: no integer encoding
      }
      lnast->add_child(fidx, carrier);
      lnast->add_child(fidx, Lnast_node::create_ref(pt));
      lnast->add_child(fidx, val);
    } else {
      Lnast_node          value = Lnast_node::create_invalid();
      std::optional<Dlop> known;  // the entry's value, when it folds here
      if (e.has_value) {
        value = expr_to_node(e.value_node);
        known = resolve_type_int_value(e.value_node);
      } else if (any_explicit || level_is_int) {
        value = seq_next;
        known = seq_value;
      } else {
        const int64_t v = parent_bits | one_hot_bit(e.name);
        value           = Lnast_node::create_const(std::to_string(v));
        known           = *Dlop::create_integer(v);
      }
      if (enc != nullptr) {
        enc->add(known);
      }
      seq_value = known ? std::optional<Dlop>(*known->add_op(*Dlop::create_integer(1))) : std::nullopt;

      if ((any_explicit || level_is_int) && !e.has_type && i + 1 < entries.size() && !entries[i + 1].has_value) {
        // Compute the next ordinal in LNAST so named constants and arbitrary
        // comptime expressions behave exactly like literal seeds.
        auto next_idx = builder.add_child(Lnast_ntype::create_plus());
        seq_next      = builder.mint_tmp_ref();
        lnast->add_child(next_idx, seq_next);
        lnast->add_child(next_idx, value);
        lnast->add_child(next_idx, Lnast_node::create_const("1"));
      }

      // Always give the entry its own carrier. Decorating the expression's
      // original ref with enum identity would rebind a named const seed.
      auto tidx = builder.add_child(Lnast_ntype::create_store());
      carrier   = builder.mint_tmp_ref();
      lnast->add_child(tidx, carrier);
      lnast->add_child(tidx, value);
    }

    // Identity tag: `carrier.__enumentry = 'NAME.entry'` (a 3-child store —
    // the `__`-prefixed key lands as a bundle ATTR leaf, so value compares
    // ignore it while enum-aware `in`/`string()` read it).
    auto sidx = builder.add_child(Lnast_ntype::create_store());
    lnast->add_child(sidx, carrier);
    lnast->add_child(sidx, Lnast_node::create_const("__enumentry"));
    lnast->add_child(sidx, Lnast_node::create_const(absl::StrCat("'", full, "'")));

    fields.emplace_back(e.name, carrier);
  }

  // The enum-type bundle: entry name → carrier. A comptime entity: `const
  // Color = enum(…)` is a comptime binding (see capture_frames_).
  auto bidx = builder.add_child(Lnast_ntype::create_tuple_add());
  auto bref = builder.mint_tmp_ref();
  lnast->add_child(bidx, bref);
  for (const auto& [k, v] : fields) {
    auto aidx = lnast->add_child(bidx, Lnast_ntype::create_store());
    lnast->add_child(aidx, Lnast_node::create_ref(k));
    lnast->add_child(aidx, v);
  }
  note_comptime_rvalue(bref);
  return bref;
}

void Prp2lnast::parse_enum_statement_entries(TSNode values, std::vector<Enum_entry>& entries) {
  if (ts_node_is_null(values)) {
    return;
  }
  for (TSNode c : ts_node_named_children(values)) {
    std::string_view t(ts_node_type(c));
    if (t == "comment") {
      continue;
    }
    // `enum Other = (...a, b=3, ...c)` (docs 03-bundle): a spread of a
    // comptime const splices in place -- a const string names one entry, a
    // const tuple splices its named fields (expand_enum_spread).
    if (t == "unary_expression") {
      TSNode op  = child_by_field(c, "operator");
      TSNode arg = child_by_field(c, "argument");
      if (!ts_node_is_null(op) && !ts_node_is_null(arg) && trim(get_text(op)) == "...") {
        expand_enum_spread(arg, entries);
      }
      continue;
    }
    Enum_entry e;
    if (t == "identifier") {
      e.name = trim(get_text(c));
    } else if (t == "typed_identifier" || t == "typed_field") {
      // `(value:Signed, add:(A, B))`: a payload entry. prpparse spells a
      // valueless `name:Type` tuple item a `typed_field`.
      TSNode id = child_by_field(c, "identifier");
      e.name    = trim(get_text(ts_node_is_null(id) ? c : id));
      if (TSNode tc = child_by_field(c, "type"); !ts_node_is_null(tc)) {
        e.type_node = tc;
        e.has_type  = true;
      }
    } else if (t == "assignment") {
      TSNode lv = child_by_field(c, "lvalue");
      TSNode rv = child_by_field(c, "rvalue");
      if (ts_node_is_null(lv)) {
        continue;
      }
      if (std::string_view(ts_node_type(lv)) == "typed_identifier") {
        TSNode id = child_by_field(lv, "identifier");
        e.name    = trim(get_text(ts_node_is_null(id) ? lv : id));
        if (TSNode tc = child_by_field(lv, "type"); !ts_node_is_null(tc)) {
          e.type_node = tc;
          e.has_type  = true;
        }
      } else {
        e.name = trim(get_text(lv));
      }
      if (!ts_node_is_null(rv)) {
        e.value_node = rv;
        e.has_value  = true;
      }
    } else {
      // Never drop an entry silently: an unlowered item would vanish from
      // the enum (and from its encoding) with no diagnostic.
      report_error(c,
                   "enum-entry-unsupported",
                   "syntax",
                   std::format("unsupported enum entry `{}`", trim(get_text(c))),
                   "an enum entry is `name`, `name=value`, `name:Type`, `name:Type=value` or a `...const` spread");
    }
    if (!e.name.empty()) {
      entries.push_back(std::move(e));
    }
  }
}

bool Prp2lnast::enum_entries_sequential(const std::vector<Enum_entry>& entries) {
  return std::any_of(entries.begin(), entries.end(), [](const Enum_entry& e) {
    return e.has_value && !e.has_type && ts_node_is_null(enum_definition_node_of(e.value_node));
  });
}

std::optional<Dlop> Prp2lnast::enum_entry_value(std::string_view enum_name, std::string_view entry) {
  // The innermost visible declaration of `enum_name` (capture frames close
  // with their scope, so a sibling lambda's same-named enum is not visible).
  TSNode decl{};
  for (auto f = capture_frames_.rbegin(); f != capture_frames_.rend() && ts_node_is_null(decl); ++f) {
    for (auto d = f->enum_decls.rbegin(); d != f->enum_decls.rend(); ++d) {
      if (TSNode nm = child_by_field(*d, "name"); !ts_node_is_null(nm) && trim(get_text(nm)) == enum_name) {
        decl = *d;
        break;
      }
    }
  }
  std::vector<Enum_entry> entries;
  if (!ts_node_is_null(decl)) {
    if (!ts_node_is_null(child_by_field(decl, "type"))) {
      return std::nullopt;  // a payload / level-typed enum
    }
    parse_enum_statement_entries(child_by_field(decl, "values"), entries);
  } else {
    // `const NAME = enum(…)`, the expression form: a visible comptime binding
    // whose kept right-hand side is the enum definition.
    const auto hit = lookup_capture(canonical_escaped_ident(enum_name));
    const auto rv  = const_rvalue_nodes_.find(canonical_escaped_ident(enum_name));
    if (!hit || hit->kind != Bind_kind::comptime || rv == const_rvalue_nodes_.end()
        || std::string_view(ts_node_type(rv->second)) != "enum_definition") {
      return std::nullopt;
    }
    parse_enum_definition_entries(rv->second, entries);
  }
  for (const auto& e : entries) {
    if (e.has_type || (e.has_value && !ts_node_is_null(enum_definition_node_of(e.value_node)))) {
      return std::nullopt;  // a payload or hierarchical entry: not a plain encoding
    }
  }
  // The values lower_enum_def emits: one-hot, or a sequence (enum_entries_sequential).
  const bool any_explicit = enum_entries_sequential(entries);
  Dlop       next         = *Dlop::create_integer(0);
  int64_t    bit          = 0;
  for (const auto& e : entries) {
    Dlop v = next;
    if (e.has_value) {
      const auto ev = resolve_type_int_value(e.value_node);
      if (!ev) {
        return std::nullopt;
      }
      v = *ev;
    } else if (!any_explicit) {
      if (bit >= kMaxOneHotEnumBits) {
        return std::nullopt;  // lower_enum_def reports it
      }
      v = *Dlop::create_integer(int64_t{1} << bit++);
    }
    if (e.name == entry) {
      return v;
    }
    next = *v.add_op(*Dlop::create_integer(1));
  }
  return std::nullopt;
}

void Prp2lnast::process_enum_assignment(TSNode n) {
  TSNode name = child_by_field(n, "name");
  if (ts_node_is_null(name)) {
    return;
  }
  reject_std_declaration(name, "enum");
  capture_frames_.back().types.insert(std::string(canonical_escaped_ident(trim(get_text(name)))));
  if (!replaying_capture_enum_) {
    // parse_next recycles the current construct's CST arena.  A later lambda
    // still needs the enum's declaration-point value, so retain only this
    // compact declaration in the persistent capture arena, on the frame of its
    // scope: it closes with that scope, so an enum local to a lambda/if body
    // never leaks into a sibling lambda (which may declare its own same-named
    // enum).
    prpparse::Ast* kept = clone_prp_subtree(retained_arena_, n.a);
    prpparse::link_parents(kept);
    capture_frames_.back().enum_decls.push_back(TSNode{kept, prp_buf.get()});
  }
  TSNode etype = child_by_field(n, "type");  // `enum Color2:Rgb = (…)` payload type

  std::vector<Enum_entry> entries;
  parse_enum_statement_entries(child_by_field(n, "values"), entries);

  Enum_encoding enc;
  auto          bref = lower_enum_def(get_text(name), etype, entries, name, 0, nullptr, &enc);

  // Mirror `type Foo = (…)`: declare with mode `type`, then bind the bundle —
  // so `mut x:Color2 = …` resolves the typename and `Color2.Green` folds.
  auto didx = builder.add_child(Lnast_ntype::create_declare());
  attach_loc(didx, n);
  lnast->add_child(didx, Lnast_node::create_ref(get_text(name)));
  lnast->add_child(didx, Lnast_ntype::create_prim_type_none());
  lnast->add_child(didx, Lnast_node::create_const("type"));
  auto sidx = builder.add_child(Lnast_ntype::create_store());
  attach_loc(sidx, n);
  lnast->add_child(sidx, Lnast_node::create_ref(get_text(name)));
  lnast->add_child(sidx, bref);

  emit_enum_encoding_alias(get_text(name), etype, enc, n);
}

void Prp2lnast::emit_enum_encoding_alias(std::string_view enum_name, TSNode etype, const Enum_encoding& enc, TSNode loc) {
  // User ruling 2026-09-28 (29): a value of an integer-encoded enum (every
  // entry value folds, no payload) is an UNSIGNED integer as wide as its
  // widest entry value needs -- one-hot `(a, b, c)` is 3 bits, `(a, b=5, c)`
  // 3 bits, a hierarchical enum one bit per node. That range rides the hidden
  // scalar alias Lnast::enum_encoding_type(NAME), which a port (upass.ssa), a
  // `reg` or a local (the runner) of the enum type resolves like any integer
  // alias (`type Byte = u8`). A negative entry makes it signed. An integer
  // level type (`enum Op:u8 = (…)`, `:unsigned(bits=4)`, an alias of one) is
  // the encoding itself. A payload enum (a tagged union) has no integer
  // encoding.
  Int_type_range level;
  if (!ts_node_is_null(etype)) {
    const auto lt = int_type_of(etype);
    if (!lt) {
      return;  // a payload level type
    }
    level = *lt;
  }
  if (enum_name.empty() || !enc.known || !enc.max || !enc.min) {
    return;
  }
  Dlop max = *enc.max;
  Dlop min = enc.min->is_negative() ? *enc.min : *Dlop::create_integer(0);
  if (level) {
    std::tie(max, min) = *level;
  } else {
    const auto bits = static_cast<uint32_t>(std::max<int64_t>(1, upass::range_bits(*enc.max, *enc.min)));
    max             = upass::max_from_bits(bits, enc.min->is_negative());
    min             = upass::min_from_bits(bits, enc.min->is_negative());
  }
  auto eidx = builder.add_child(Lnast_ntype::create_declare());
  attach_loc(eidx, loc);
  lnast->add_child(eidx, Lnast_node::create_ref(Lnast::enum_encoding_type(enum_name)));
  auto tidx = lnast->add_child(eidx, Lnast_ntype::create_prim_type_int());
  lnast->add_child(tidx, Lnast_node::create_const(std::string(max.to_pyrope())));
  lnast->add_child(tidx, Lnast_node::create_const(std::string(min.to_pyrope())));
  lnast->add_child(eidx, Lnast_node::create_const("type"));
}

std::optional<Prp2lnast::Int_type_range> Prp2lnast::int_type_of(TSNode ty) const {
  if (ts_node_is_null(ty)) {
    return std::nullopt;
  }
  if (TSNode inner = child_by_field(ty, "type"); !ts_node_is_null(inner)) {
    ty = inner;  // a type_cast wrapper
  }
  while (std::string_view(ts_node_type(ty)) == "expression_type" && ts_node_named_child_count(ty) == 1) {
    ty = ts_node_named_child(ty, 0);
  }
  const std::string_view t(ts_node_type(ty));
  if (t == "uint_type" || t == "sint_type") {
    return folded_int_type_range(ty);
  }
  if (t != "identifier") {
    return std::nullopt;
  }
  const auto name = trim(get_text(ty));
  for (auto f = capture_frames_.rbegin(); f != capture_frames_.rend(); ++f) {
    if (const auto it = f->int_type_aliases.find(name); it != f->int_type_aliases.end()) {
      return it->second;
    }
  }
  return std::nullopt;
}

void Prp2lnast::process_type_statement(TSNode n) {
  TSNode name = child_by_field(n, "name");
  if (ts_node_is_null(name)) {
    return;
  }
  reject_std_declaration(name, "type");
  // `pub type X = …`: an exportable type alias — registered as a pub of kind
  // "type" so an importer's `x:pkg.X` can bind it (the alias's scalar range
  // rides pub_values as its `uN`/`sN` text — see harvest_pub_values).
  if (!ts_node_is_null(child_by_field(n, "pub"))) {
    if (!builder.at_top_stmts() || !lambda_kind_stack_.empty() || conditional_depth_ > 0) {
      report_error(n,
                   "pub-not-file-scope",
                   "syntax",
                   "`pub` is only valid on file-scope declarations",
                   "move the type alias to the file's top level");
    }
    lnast->add_pub(trim(get_text(name)), "type", mint_src(name));
  }
  // `type Foo = …` is a declaration whose mode is `type` (replaces
  // the former type_def node). The type slot is normally `prim_type_none` and
  // the mode const carries "type" — EXCEPT for a SCALAR primitive alias
  // (`type Foo = U10`/`S8`/`Bool`/`String`/`Clock`/`Reset`), where the resolved
  // `prim_type_int(max,min)` lands in the type slot so a later `:Foo`
  // annotation borrows the width (a scalar type carries no field bundle, so
  // the store-the-tuple path below cannot preserve it). A named-identifier
  // alias (`type Foo = Bar`) carries `ref Bar`; a tuple type keeps
  // `prim_type_none` and describes its fields with `type_spec` rows.
  TSNode     alias = child_by_field(n, "alias");
  const bool scalar_prim_alias
      = !ts_node_is_null(alias)
        && (std::string_view(ts_node_type(alias)) == "uint_type" || std::string_view(ts_node_type(alias)) == "sint_type"
            || std::string_view(ts_node_type(alias)) == "bool_type" || std::string_view(ts_node_type(alias)) == "string_type"
            || std::string_view(ts_node_type(alias)) == "clock_type" || std::string_view(ts_node_type(alias)) == "reset_type");

  // An alias of another NAMED type (`type Loc_t = pk.Req_t`, `type A = B`)
  // keeps its target as a `ref` in the type slot, so a port typed `Loc_t`
  // resolves to the target's layout.
  bool named_alias = !ts_node_is_null(alias) && std::string_view(ts_node_type(alias)) == "expression_type"
                     && ts_node_named_child_count(alias) != 0;
  for (uint32_t i = 0; named_alias && i < ts_node_named_child_count(alias); ++i) {
    named_alias = std::string_view(ts_node_type(ts_node_named_child(alias, i))) == "identifier";
  }

  if (scalar_prim_alias) {
    // `type Row = Unsigned(bits=N)` in a generic body: the bound only folds
    // once N is bound, so it defers like a variable declaration's.
    prelower_int_type_bounds(alias);
  }
  capture_frames_.back().types.insert(std::string(canonical_escaped_ident(trim(get_text(name)))));
  auto idx = builder.add_child(Lnast_ntype::create_declare());
  lnast->add_child(idx, Lnast_node::create_ref(get_text(name)));
  if (scalar_prim_alias) {
    emit_type_expr(idx, alias);  // prim_type_int(max,min) / _bool / _string / _clock / _reset
    if (const std::string_view at(ts_node_type(alias)); at == "uint_type" || at == "sint_type") {
      capture_frames_.back().int_type_aliases.insert_or_assign(std::string(trim(get_text(name))), folded_int_type_range(alias));
    }
  } else if (named_alias) {
    if (const auto target = int_type_of(alias)) {
      capture_frames_.back().int_type_aliases.insert_or_assign(std::string(trim(get_text(name))), *target);
    }
    // The target must be a type in scope (`type T = Nope_t` is as wrong as a
    // `:Nope_t` annotation), and not the removed `I<N>` spelling.
    check_type_name_spelling(alias);
    record_type_name_read(alias);
    lnast->add_child(idx, Lnast_node::create_ref(trim(get_text(alias))));
  } else {
    lnast->add_child(idx, Lnast_ntype::create_prim_type_none());
  }
  lnast->add_child(idx, Lnast_node::create_const("type"));

  // Keep the type's bundle as a VALUE in the symbol table so a later
  // `mut v:Foo = (…)` can materialize Foo's default fields/values (named-type
  // borrowing — see upass_constprop process_assign). `type Foo = (tuple)` puts
  // the tuple in the `alias` field (the `= _type` form) or `definition` (the
  // no-`=` `type Foo(…)` trait form). A `type Foo = OtherType` alias (an
  // identifier/scalar type, not a tuple) carries no field bundle, so we only
  // store a tuple RHS. Mirrors the `const Foo = (…)` lowering.
  TSNode rhs = child_by_field(n, "definition");  // `type Foo ( … )` trait form
  if (ts_node_is_null(rhs)) {
    rhs = alias;  // `type Foo = …` form (an expression_type / primitive type)
  }
  // `type Foo = (…)` lands the tuple inside an `expression_type` wrapper; unwrap
  // to the inner `tuple`. (`type Foo = OtherType` wraps a scalar/identifier type
  // with no field bundle — skipped below since it is not a tuple.)
  if (!ts_node_is_null(rhs) && std::string_view(ts_node_type(rhs)) == "expression_type") {
    for (uint32_t i = 0, nc = ts_node_child_count(rhs); i < nc; i++) {
      TSNode c = ts_node_child(rhs, i);
      if (std::string_view(ts_node_type(c)) == "tuple") {
        rhs = c;
        break;
      }
    }
  }
  if (!ts_node_is_null(rhs) && std::string_view(ts_node_type(rhs)) == "tuple") {
    // Named tuple aliases need the same nested leaf layout as an anonymous
    // declaration; expr_to_node intentionally skips tuple-typed bare fields.
    emit_tuple_type_field_specs(get_text(name), rhs);
    // Lower the bundle FIRST (emits the tuple_add statement), then build the
    // store referencing its result — so the producer precedes the consumer.
    auto rhs_val = expr_to_node(rhs);
    auto sidx    = builder.add_child(Lnast_ntype::create_store());
    lnast->add_child(sidx, Lnast_node::create_ref(get_text(name)));
    lnast->add_child(sidx, rhs_val);
  }
}

// Defined with the interpolated-string lowering below; needed early by the
// import-argument validation.
static std::string unescape_cooked_string(std::string_view raw);

// The `import` builtin (the LiveHD docs).
// Returns the unquoted body when `n` is a plain comptime string literal
// expression — `'…'` (raw) or `"…"` with no `{…}` interpolation — else nullopt.
std::optional<std::string> Prp2lnast::plain_string_literal_text(TSNode n) {
  if (ts_node_is_null(n)) {
    return std::nullopt;
  }
  std::string_view t(ts_node_type(n));
  if ((t == "constant" || t == "paren_group") && ts_node_named_child_count(n) == 1) {
    return plain_string_literal_text(ts_node_named_child(n, 0));
  }
  if (t == "string_literal") {  // single-quoted raw literal: '…'
    auto txt = trim(get_text(n));
    if (txt.size() >= 2) {
      return std::string(txt.substr(1, txt.size() - 2));
    }
    return std::nullopt;
  }
  if (t == "interpolated_string_literal") {  // double-quoted; reject `{expr}` parts
    if (ts_node_end_byte(n) < ts_node_start_byte(n) + 2) {
      return std::nullopt;
    }
    // Same decoding as interpolated_string_to_node: a comment-only hole is an
    // empty hole (literal `{}`), never text that leaks into the value.
    auto pieces = istring_pieces(n);
    if (pieces.size() != 1 || pieces[0].is_hole) {
      return std::nullopt;
    }
    return std::move(pieces[0].text);
  }
  return std::nullopt;
}

// The canonical marked-builtin call shape:
//   func_call(target, const "import", const '<unit>')
// — exactly what `lhd scan` (collect_imports) matches and the upass resolver
// folds. The callee is a CONST (not a ref) so constprop
// leaves it unfolded and no user `import` definition can capture it.
//
// `foo` and foo are the same identifier: every `.`-separated segment of a plain
// unit/member path is canonicalized (`import("lib.`name`")` ≡
// `import("lib.name")`, while `` `in c` `` keeps its escape) so the unit key
// matches the canonical names prp2lnast stamps on `pub` entries and trees. The
// `ln:`/`lg:` forms name a tree/graph verbatim and are left alone.
static std::string canonical_import_unit(std::string_view unit) {
  if (unit.starts_with("ln:") || unit.starts_with("lg:")) {
    return std::string(unit);
  }
  return str_tools::canonical_escaped_path(unit);
}

void Prp2lnast::emit_import_call(const Lnast_node& target, std::string_view unit_src, TSNode loc_node) {
  const std::string unit = canonical_import_unit(unit_src);
  auto              idx  = builder.add_child(Lnast_ntype::create_func_call());
  lnast->add_child(idx, target);
  lnast->add_child(idx, Lnast_node::create_const("import"));
  lnast->add_child(idx, Lnast_node::create_const(absl::StrCat("'", unit, "'")));
  attach_loc(idx, loc_node);  // → resolution diagnostics point at the call site
  note_comptime_rvalue(target);
  if (conditional_depth_ == 0 && target.is_ref()) {
    capture_import_bindings_[std::string(target.get_name())] = absl::StrCat("'", unit, "'");
  }
}

// Expression form `import("unit")`: validate the argument (exactly one, a
// comptime string literal — imports resolve statically, so computed strings
// cannot name a unit) and lower to the canonical call.
void Prp2lnast::lower_import_call(TSNode call_node, TSNode arg_tuple, const Lnast_node& target) {
  std::vector<TSNode> items;
  if (!ts_node_is_null(arg_tuple)) {
    for (TSNode c : ts_node_named_children(arg_tuple)) {
      if (std::string_view(ts_node_type(c)) == "comment") {
        continue;
      }
      items.push_back(c);
    }
  }
  if (items.size() != 1) {
    report_error(call_node,
                 "import-arity",
                 "syntax",
                 "`import` takes exactly one argument",
                 "write `import(\"unit\")`, `import(\"ln:unit.tree\")`, or `import(\"lg:graph\")`");
  }
  auto text = plain_string_literal_text(items.front());
  if (!text || text->empty()) {
    report_error(items.front(),
                 "import-not-literal",
                 "syntax",
                 "the `import` argument must be a comptime string literal",
                 "imports resolve statically — a computed string cannot name a unit");
  }
  emit_import_call(target, *text, call_node);
}

// Statement form `import "unit" as b` — exact sugar for
// `const b = import("unit")`: a const DECLARATION (declare via the decl-merge
// + store), so read-only-always and the normal no-shadowing / redeclaration
// rules apply unchanged.
void Prp2lnast::process_import_statement(TSNode n) {
  TSNode alias = child_by_field(n, "alias");
  TSNode mod   = child_by_field(n, "module");
  // String-literal-only: the old dotted-identifier grammar form
  // (`import a.b.c as x`) is rejected.
  auto   text  = plain_string_literal_text(mod);
  if (!text || text->empty()) {
    report_error(ts_node_is_null(mod) ? n : mod,
                 "import-not-literal",
                 "syntax",
                 "the imported unit must be a comptime string literal",
                 "write `import \"unit\" as name`");
  }
  if (ts_node_is_null(alias)) {
    report_error(n, "import-needs-alias", "syntax", "`import … as <name>` requires an alias", "write `import \"unit\" as name`");
  }

  Lnast_node tmp = builder.mint_tmp_ref();
  emit_import_call(tmp, *text, n);

  reject_std_declaration(alias, "import alias");
  Lnast_node ref = Lnast_node::create_ref(canonical_escaped_ident(trim(get_text(alias))));
  note_binding(ref.get_name(), Bind_kind::comptime);
  if (conditional_depth_ == 0) {
    capture_import_bindings_[std::string(ref.get_name())] = absl::StrCat("'", canonical_import_unit(*text), "'");
  }
  {
    auto idx = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(idx, ref);
    lnast->add_child(idx, Lnast_node::create_const("type"));
    lnast->add_child(idx, Lnast_node::create_const("const"));
    // Span → the decl-merge copies this onto the synthesized `declare` so
    // declaration-site diagnostics point at the alias.
    attach_loc(idx, alias);
  }
  {
    auto idx = builder.add_child(Lnast_ntype::create_store());
    lnast->add_child(idx, ref);
    lnast->add_child(idx, tmp);
    attach_loc(idx, alias);
  }
}

void Prp2lnast::process_test_statement(TSNode n) {
  ++simulation_test_depth_;
  TSNode      code = child_by_field(n, "code");
  // The grammar splits the dotted test selector (f_name: `test_name`, e.g.
  // `counter.held_high`) from the optional runtime parameter list (f_input: an
  // `arg_list`, typed-with-defaults exactly like a comb's inputs). The selector
  // lets the sim runner pick a test; the parameters are bound per `--arg`.
  std::string test_name;
  if (TSNode name = child_by_field(n, "name"); !ts_node_is_null(name)) {
    test_name = str_tools::canonical_escaped_path(trim(get_text(name)));  // `cnt`.`basic` == cnt.basic
  }
  Capture_frame_guard test_frame(capture_frames_, Capture_frame{.test_scope = true, .lambda_name = test_name});

  auto fd_idx = builder.add_child(Lnast_ntype::create_func_def());
  auto tmp    = builder.mint_tmp_ref();
  lnast->add_child(fd_idx, tmp);
  lnast->add_child(fd_idx, Lnast_node::create_const("comb"));
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());                // generics
  auto in_idx = lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());  // inputs
  // Lower the test's `(params)` into the comb's inputs so body reads of a
  // parameter resolve (its visibility comes from the func_def signature, see
  // read_is_visible / prp_collect_sig_targets). The default-value expression is
  // the parameter's value when `--arg` does not override it. An in-flight
  // signature frame lets a later default read an earlier parameter.
  inflight_name_scopes_.emplace_back();
  if (TSNode inp = child_by_field(n, "input"); !ts_node_is_null(inp)) {
    TSNode pending_typed{};
    TSNode pending_def{};
    auto   flush = [&]() {
      if (!ts_node_is_null(pending_typed)) {
        emit_arg_assign(in_idx, pending_typed, pending_def, /*is_ref_mod=*/false, nullptr, "comb", /*is_io_output=*/false);
        if (TSNode id = child_by_field(pending_typed, "identifier"); !ts_node_is_null(id)) {
          // User ruling 2026-09-28 (31): a test parameter named like a visible
          // file-scope const is shadowing, like a test local; like a lambda
          // input (35), one named like a type or a lambda (the DUT) is too.
          const auto pname = canonical_escaped_ident(get_text(id));
          check_capture_shadow(pname, id);
          check_signature_shadow(pname, id, "test parameter");
          inflight_name_scopes_.back().emplace_back(get_text(id));
          // A runtime value of the test frame, like a comb input: a nested
          // comb that reads a const derived from it is a runtime capture.
          auto& pb = note_binding(pname, Bind_kind::runtime);
          if (TSNode pt = child_by_field(pending_typed, "type"); !ts_node_is_null(pt)) {
            pb.typed = true;
            pb.range = folded_int_type_range(child_by_field(pt, "type"));
          }
        }
      }
      pending_typed = TSNode{};
      pending_def   = TSNode{};
    };
    uint32_t nc = child_count(inp);
    for (uint32_t i = 0; i < nc; i++) {
      TSNode           ci    = child(inp, i);
      const char*      fname = ts_node_field_name_for_child(inp, i);
      std::string_view ct(ts_node_type(ci));
      if (ct == "typed_identifier") {
        flush();
        pending_typed = ci;
      } else if (fname && std::string_view(fname) == "definition" && ct != "=") {
        pending_def = ci;
      }
    }
    flush();
  }
  inflight_name_scopes_.pop_back();
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());  // outputs
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
  if (!test_name.empty()) {
    auto nidx = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(nidx, tmp);
    lnast->add_child(nidx, Lnast_node::create_const("test_name"));
    lnast->add_child(nidx, Lnast_node::create_const(absl::StrCat("'", test_name, "'")));
  }
  --simulation_test_depth_;
}

void Prp2lnast::process_spawn_statement(TSNode n) {
  // `spawn name = { … }` was REMOVED (docs/pyrope/09-verification.md,
  // 15-tbd.md): a testbench is one deterministic `tick`/`step` loop, with no
  // scheduler and no `spawn`/`join`/`cancel`. The pinned @prpparse grammar
  // still parses the statement (PRP_KEYWORD(spawn) / spawn_statement), so it
  // is refused HERE rather than left to the unhandled-statement warning: the
  // old lowering minted an EMPTY `comb` (the body was never traversed) and the
  // program compiled with 0 errors while silently missing code.
  report_error(n,
               "spawn-removed",
               "type",
               "`spawn` was removed — a testbench is one `tick`/`step` loop, with no concurrent threads",
               "move the body into the enclosing `test` block (or a `comb` it calls): wait on a condition with `step` and "
               "`if not <cond> { continue }`, and check an invariant with an `if`-block inside the loop");
}

void Prp2lnast::process_impl_statement(TSNode n) {
  // Simplified: emit as a placeholder assert to preserve node count.
  auto idx = builder.add_child(Lnast_ntype::create_cassert());
  attach_loc(idx, n);
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
  if (t == "timed_identifier") {
    // `tmp@[3]` on a RHS: the VALUE is the plain identifier read;
    // `@[3]` is a pure landing-cycle check (never a flop / never a
    // delay_assign), recorded as an inert timecheck statement.
    TSNode inner = child_by_field(n, "identifier");
    if (!ts_node_is_null(inner)) {
      maybe_emit_timecheck(child_by_field(n, "timing"), inner);
      return identifier_to_node(inner, /*for_lvalue=*/false);
    }
  }
  if (t == "expression_item") {
    return binary_expr_to_node(n);
  }
  if (t == "enum_definition") {
    // `const Color = enum(Yellow:Rgb = …, Green = Rgb(…))`. The entry-name
    // prefix ("Color.Yellow") comes from the enclosing assignment's lvalue.
    std::string ename;
    for (TSNode p = ts_node_parent(n); !ts_node_is_null(p); p = ts_node_parent(p)) {
      if (std::string_view(ts_node_type(p)) != "assignment") {
        continue;
      }
      TSNode lv = child_by_field(p, "lvalue");
      if (!ts_node_is_null(lv)) {
        TSNode id = std::string_view(ts_node_type(lv)) == "typed_identifier" ? child_by_field(lv, "identifier") : TSNode{};
        ename     = trim(get_text(ts_node_is_null(id) ? lv : id));
      }
      break;
    }
    std::vector<Enum_entry> entries;
    parse_enum_definition_entries(n, entries);
    // `const Color = enum(…)` is a type too (`c:Color`, `mut y:Color`), with
    // the same integer encoding as `enum Color = (…)` (user ruling 29).
    Enum_encoding enc;
    auto          bref = lower_enum_def(ename, TSNode{}, entries, n, 0, nullptr, &enc);
    emit_enum_encoding_alias(ename, TSNode{}, enc, n);
    return bref;
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
    auto txt = trim(get_text(n));
    check_binary_literal_sign(txt, n);
    return constant_text_to_node(txt);
  }
  if (t == "integer_literal" || t == "bool_literal" || t == "unknown_literal" || t == "string_literal") {
    auto txt = trim(get_text(n));
    check_binary_literal_sign(txt, n);
    return constant_text_to_node(txt);
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
    // A single-item parenthesized expression with NO trailing comma is grouping
    // (`(x)` == x), not a 1-element tuple — only `(x,)` builds a 1-tuple. The
    // grammar parses both as a single-item `tuple` (the `,` is a hidden token),
    // so detect a trailing comma in the source text after the item. Without this,
    // `s = (b & 15)` lowers as a 1-tuple whose tuple_add tolg cannot lower (an
    // inlined caller folds the 1-tuple away; a standalone module reads it as nil).
    if (ts_node_named_child_count(n) == 1) {
      TSNode           it = ts_node_named_child(n, 0);
      std::string_view itt(ts_node_type(it));
      // Plain value item only: an `assignment` (`a=1`) is a genuine 1-element
      // bundle, never grouping. A `unary_expression` is grouping too — `(-x)`,
      // `(~x)`, `(not x)` unwrap so the inner value keeps its scalar kind (a
      // 1-tuple would stamp Kind::tuple and a parent `+`/`&`/`==` would reject
      // it; see 2f-unary_type) — EXCEPT a spread `...x`, which is a genuine
      // 1-element bundle.
      // A `typed_field` (`(val:Signed)`) is a one-field tuple TYPE, never grouping.
      bool             plain = itt != "assignment" && itt != "typed_field" && itt != "comment";
      if (itt == "unary_expression") {
        TSNode op_n = child_by_field(it, "operator");
        plain       = !ts_node_is_null(op_n) && std::string_view(ts_node_type(op_n)) != "op_spread";
      }
      const auto tail = text_between(ts_node_end_byte(it), ts_node_end_byte(n));
      if (plain && tail.find(',') == std::string_view::npos) {
        return expr_to_node(it);  // grouping — the value itself
      }
    }
    return tuple_to_node(n, /*is_square=*/false);
  }
  if (t == "tuple_sq") {
    return tuple_to_node(n, /*is_square=*/true);
  }
  if (t == "paren_group") {
    // Single-expression parenthesized grouping introduced in the new grammar
    // (`(expr).foo`, `(expr)#[..]`, `(expr):type`). Pass through to the inner
    // expression — the parens carry no value semantics on their own.
    uint32_t nnc = ts_node_named_child_count(n);
    if (nnc >= 1) {
      return expr_to_node(ts_node_named_child(n, 0));
    }
    return Lnast_node::create_const("0");
  }
  if (t == "typed_identifier") {
    TSNode id = child_by_field(n, "identifier");
    if (!ts_node_is_null(id)) {
      return identifier_to_node(id, false);
    }
  }
  if (t == "attribute_set") {
    return attribute_set_to_node(n);
  }
  if (t == "typed_field") {
    // Bare `name:type` tuple-type field (`(x:u3, y:s4)`, enum bodies, type
    // shapes for `does`/`equals`/`case`). The name is the field ref; the
    // type lowers as a type_spec against it.
    TSNode     id   = child_by_field(n, "identifier");
    Lnast_node aref = ts_node_is_null(id) ? Lnast_node::create_const("nil") : expr_to_node(id);
    TSNode     tc   = child_by_field(n, "type");
    // 2f-nested_type — a SCALAR field's type_spec against the bare field ref is
    // harmless (it is what `does`/`equals`/enum bodies consume), but a
    // TUPLE-shaped field's would emit a statement-level `store(ref '<field>',
    // %tmp)` against a name that was never declared. The shape is stamped by
    // emit_tuple_type_field_specs on the dotted path instead, so skip it here.
    //
    // GAP (reported, not fixed here): only a DECLARATION reaches this node
    // through emit_type_spec, which does that dotted stamping. A `does`/
    // `equals`/`case`/enum body with a nested tuple-typed field reaches it
    // directly, and there the shape is now dropped with no diagnostic.
    if (!ts_node_is_null(tc) && ts_node_is_null(tuple_type_inner(tc))) {
      emit_type_spec(aref, tc);
    }
    return aref;
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
    // Code-block-as-expression: run the block's statements in a nested scope and
    // yield the value of its LAST expression (05b-statements.md "Code block").
    // Inner declarations stay scoped to the block (Conditional_scope bump). A
    // block whose last item is a statement (assignment/decl/control) has no
    // value → nil. (2f-codeblock_expr — was unconditionally folding to 0/nil.)
    static const absl::flat_hash_set<std::string_view> block_stmt_kinds = {
        "declaration_statement",
        "assignment",
        "while_statement",
        "for_statement",
        "loop_statement",
        "control_statement",
        "lambda",
        "enum_assignment",
        "type_statement",
        "import_statement",
        "test_statement",
        // still parsed by the pinned grammar; its dispatch is the `spawn-removed` error
        "spawn_statement",
        "impl_statement",
        "scope_statement",
    };
    Conditional_scope   guard(&conditional_depth_);
    Capture_frame_guard frame(capture_frames_, Capture_frame{});
    // Collected once: the block can be huge and both the reverse last-non-
    // comment scan and the forward walk need indexed access.
    std::vector<TSNode> kids;
    for (TSNode kc : ts_node_named_children(n)) {
      kids.push_back(kc);
    }
    const uint32_t nc   = static_cast<uint32_t>(kids.size());
    int            last = -1;
    for (int i = static_cast<int>(nc) - 1; i >= 0; --i) {
      if (std::string_view(ts_node_type(kids[static_cast<uint32_t>(i)])) != "comment") {
        last = i;
        break;
      }
    }
    Lnast_node result = Lnast_node::create_const("nil");
    for (uint32_t i = 0; i < nc; i++) {
      TSNode           c = kids[i];
      std::string_view ct(ts_node_type(c));
      if (ct == "comment") {
        continue;
      }
      if (static_cast<int>(i) == last && !block_stmt_kinds.contains(ct)) {
        result = expr_to_node(c);  // block value = last expression
      } else {
        process_statement(c);
      }
    }
    return result;
  }
  if (t == "interpolated_string_literal") {
    return interpolated_string_to_node(n);
  }
  // Fallback: treat as a constant from source text.
  return constant_text_to_node(trim(get_text(n)));
}

// Decode a double-quoted string's literal text (docs 02-basics "Strings"): the
// escapes \n \t \r \\ \" \' \` \0 \xNN \u{N}, and a DOUBLED brace is a
// literal brace (`{{` is `{`, `}}` is `}`; `\{` is not an escape). The lexer
// already rejected every other `\c` and a lone `}`, so a malformed sequence
// here is kept verbatim. Single-quoted (raw) strings never reach here.
static std::string unescape_cooked_string(std::string_view raw) {
  auto hex_digit = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
      return 10 + (c - 'A');
    }
    return -1;
  };
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    char c = raw[i];
    if ((c == '{' || c == '}') && i + 1 < raw.size() && raw[i + 1] == c) {
      out.push_back(c);  // `{{` / `}}`: one literal brace
      ++i;
      continue;
    }
    if (c != '\\' || i + 1 >= raw.size()) {
      out.push_back(c);
      continue;
    }
    char next = raw[i + 1];
    switch (next) {
      case 'n':
        out.push_back('\n');
        ++i;
        break;
      case 't':
        out.push_back('\t');
        ++i;
        break;
      case 'r':
        out.push_back('\r');
        ++i;
        break;
      case '0':
        out.push_back('\0');
        ++i;
        break;
      case '\\':
      case '"':
      case '\'':
      case '`':
        out.push_back(next);
        ++i;
        break;
      case 'x': {
        int hi = (i + 2 < raw.size()) ? hex_digit(raw[i + 2]) : -1;
        int lo = (i + 3 < raw.size()) ? hex_digit(raw[i + 3]) : -1;
        if (hi >= 0 && lo >= 0) {
          out.push_back(static_cast<char>((hi << 4) | lo));
          i += 3;
        } else {
          out.push_back(c);  // malformed \x — keep verbatim
        }
        break;
      }
      case 'u': {
        // `\u{N}`: 1..6 hex digits, UTF-8 encoded (the backtick-name decoder
        // in str_tools reads the identical set).
        const auto close = (i + 2 < raw.size() && raw[i + 2] == '{') ? raw.find('}', i + 3) : std::string_view::npos;
        uint32_t   cp    = 0;
        bool       ok    = close != std::string_view::npos && close > i + 3 && close - (i + 3) <= 6;
        for (size_t k = i + 3; ok && k < close; ++k) {
          const int h = hex_digit(raw[k]);
          ok          = h >= 0;
          cp          = cp * 16 + static_cast<uint32_t>(std::max(h, 0));
        }
        if (!ok || cp > 0x10FFFF) {
          out.push_back(c);  // malformed \u — keep verbatim
          break;
        }
        if (cp < 0x80) {
          out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
          out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
          out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
          out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
          out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
          out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        i = close;
        break;
      }
      default:
        out.push_back(c);  // unknown escape (the lexer rejects it) — keep backslash
        break;
    }
  }
  return out;
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

std::vector<Prp2lnast::Istring_piece> Prp2lnast::istring_pieces(TSNode n) const {
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
      lit += unescape_cooked_string(text_between(seg, upto));
    }
  };

  // Walk the body exactly like prpparse's Parser::parse_istring (the parser
  // that produced the hole expressions): an escape pair is text, `{{` is a
  // literal brace pair, and every other `{` opens a hole whose end the LEXER
  // finds (comments, nested strings and backtick names are skipped there).
  const prpparse::Lexer lex(*prp_buf);
  uint32_t              i = body_start;
  while (i < body_end) {
    const char c = prp_file[i];
    if (c == '\\') {
      // `\u{N}` carries its own braces: skip them whole (they open no hole).
      if (i + 2 < body_end && prp_file[i + 1] == 'u' && prp_file[i + 2] == '{') {
        uint32_t k = i + 3;
        while (k < body_end && prp_file[k] != '}') {
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
    if (i + 1 < body_end && prp_file[i + 1] == '{') {
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
        report_error(expr,
                     "bad-interpolation",
                     "syntax",
                     std::format("unexpected `{}` after the expression in a string interpolation hole", gap),
                     "a hole holds one expression and an optional `:format` spec: \"{expr}\" or \"{expr:b}\"");
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

Lnast_node Prp2lnast::interpolated_string_to_node(TSNode n) {
  // Double-quoted string body, possibly containing `{expr[:fmt]}` holes. With
  // no expression hole, re-emit the body as a single-quoted pyrope string
  // literal that `Dlop::from_pyrope` can round-trip (escape sequences \n, \t,
  // \\, \", \xNN are decoded by istring_pieces; the resulting bytes are
  // embedded directly inside the single-quoted wrapper).
  auto pieces = istring_pieces(n);
  if (pieces.size() == 1 && !pieces[0].is_hole) {
    return Lnast_node::create_const(absl::StrCat("'", pieces[0].text, "'"));
  }

  // Lower to `string("part0", expr0, "part1", expr1, ..., "partN")`. The
  // constprop pass folds calls whose every arg is a comptime constant into a
  // single string literal; otherwise the call survives at runtime.
  std::vector<Lnast_node> args;
  for (auto& p : pieces) {
    if (!p.is_hole) {
      args.push_back(Lnast_node::create_const(absl::StrCat("'", p.text, "'")));
      continue;
    }
    auto value_node = expr_to_node(p.expr);
    if (p.spec.empty()) {
      args.push_back(value_node);
    } else {
      // `{expr:fmt}` → render `expr` through `__fmt(expr, 'fmt')`, an internal
      // cast that constprop folds into a string (e.g. `:b` → binary digits).
      // The enclosing `string(...)` then concatenates the rendered text. Emit
      // it here (before the outer string() call) so its tmp folds first.
      auto fidx = builder.add_child(Lnast_ntype::create_func_call());
      auto fref = builder.mint_tmp_ref();
      lnast->add_child(fidx, fref);
      lnast->add_child(fidx, Lnast_node::create_ref("__fmt"));
      lnast->add_child(fidx, value_node);
      lnast->add_child(fidx, Lnast_node::create_const(absl::StrCat("'", p.spec, "'")));
      args.push_back(fref);
    }
  }

  auto idx = builder.add_child(Lnast_ntype::create_func_call());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, Lnast_node::create_ref("String"));
  for (auto& a : args) {
    lnast->add_child(idx, a);
  }
  return ref;
}

void Prp2lnast::check_binary_literal_sign(std::string_view text, const TSNode& node) const {
  // Binary literals must carry an explicit sign: `0ub…` (unsigned) or `0sb…`
  // (signed). The bare `0b…` form is no longer valid — it left the signedness
  // ambiguous. Detect `0` immediately followed by `b`/`B` (skipping an optional
  // leading `-`/`+`); a valid binary has `s`/`u` between the `0` and the `b`.
  size_t i = 0;
  if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
    ++i;
  }
  if (i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'b' || text[i + 1] == 'B')) {
    auto bits = text.substr(i + 2);
    report_error(node,
                 "invalid-binary-prefix",
                 "syntax",
                 std::format("binary literal `0b…` is missing its sign: use `0ub{}` (unsigned) or `0sb{}` (signed)", bits, bits),
                 "prefix binary constants with 0ub (unsigned) or 0sb (signed)");
  }
}

Lnast_node Prp2lnast::constant_text_to_node(std::string_view text) {
  if (text.empty()) {
    return Lnast_node::create_const("0");
  }
  return Lnast_node::create_const(std::string(text));
}

// Pyrope's `…` escape lets identifiers carry spaces/punctuation. When the escaped
// text is a plain alnum/underscore identifier (`` `a` `` ↔ `a`), the canonical
// symbol name drops the backticks so a DECLARATION and every READ resolve to the
// SAME name — a `` `name` `` port (e.g. a reserved word like `in` emitted by
// prp_writer) must round-trip. The escape is kept only when it actually carries a
// special character (or the inner text starts with a digit). Shared by
// identifier_to_node (reads) and the param/port declaration sites (emit_arg_assign
// / typed_field) so both ends canonicalize identically.
[[nodiscard]] static std::string_view canonical_escaped_ident(std::string_view name) {
  // One definition, in core/str_tools.hpp: the consumers that look names up
  // (the symbol table, call_resolver's import-member match) must apply the
  // identical rule, so it cannot live here as a private copy.
  return str_tools::canonical_escaped_ident(name);
}

Lnast_node Prp2lnast::identifier_to_node(TSNode n, bool for_lvalue) {
  auto name = trim(get_raw_text(n));
  // Keyword constants that fell through as identifiers (raw text: an escaped
  // `` `true` `` is a name, not the literal).
  if (name == "true" || name == "false" || name == "nil") {
    if (for_lvalue) {
      // Binding a keyword constant (`const nil = a`) would assign into a
      // constant and silently drop the value; the parser already rejects the
      // `true`/`false` forms the same way.
      report_error(n,
                   "reserved-word-as-name",
                   "syntax",
                   std::format("'{}' is a reserved word, so it cannot be a variable name", name),
                   std::format("wrap it in backticks (`{}`) to use it as an identifier", name));
    }
    return Lnast_node::create_const(name);
  }
  if (!for_lvalue && str_tools::is_pyrope_type_word(name)) {
    // A bare type word used as a value (`(a=U8, b=S20)`, `[Bool, Clock]`,
    // `U8.[max]`): the TYPE, never a variable (a type word is reserved; the
    // backticked `` `U8` `` is the name and keeps its backticks).
    return type_word_value(n, name);
  }
  if (name.size() >= 3 && name.front() == '`' && name.find('\\') != std::string_view::npos) {
    // A backtick name reads the string escapes (`` `i\x41` `` is the name `iA`):
    // get_text decodes and canonicalizes, so declarations and reads agree.
    name = get_text(n);
  }
  name = canonical_escaped_ident(name);
  if (for_lvalue) {
    return Lnast_node::create_ref(name);
  }
  // Value (read) context: record the source site plus the builder position so
  // check_undefined_reads can resolve it lexically (read-before-declaration,
  // out-of-scope and never-declared reads are all compile errors). Names
  // introduced by an in-flight construct (earlier tuple-literal fields,
  // earlier lambda params feeding a default value) are resolved right here —
  // they have no statement-level declaration the scope walk could find.
  if (!name_in_inflight_scope(name)) {
    note_capture_read(name, n);
    // Capture the read site's span NOW (2f-stream): the streaming arena reset
    // means `n` (its Ast) will not outlive this construct, but the undefined-read
    // check runs after the whole walk.
    push_read_site(n, std::string{name}, /*no_frame=*/false);
  }
  return Lnast_node::create_ref(name);
}

Lnast_node Prp2lnast::attribute_set_to_node(TSNode n) {
  // `expr::[attr=…]` — argument + attribute_sq carrier. Expression-level
  // `expr:type` is no longer Pyrope (types appear only at declaration sites
  // and inside tuple types), so there is no `type` field here.
  // The `attribute` field tags the anonymous `seq(':', attribute_sq)`, so
  // `child_by_field` would return the `:` token — walk children for the
  // attribute_sq node instead.
  TSNode     arg  = child_by_field(n, "argument");
  Lnast_node aref = expr_to_node(arg);
  for (TSNode c : ts_node_named_children(n)) {
    std::string_view ct(ts_node_type(c));
    if (ct == "attribute_sq" || ct == "attribute_list" || ct == "tuple_sq") {
      emit_attribute_list(aref, c);
    }
  }
  return aref;
}

// Largest int-type bit width whose bounds we materialize. A wider type (e.g.
// `u2147483647`) makes Dlop::get_mask_value build a multi-gigabit constant and
// hang, so the two `u<N>/s<N>/i<N>` parse sites reject anything above this with
// a clean diagnostic. Generous vs. any real design (buses are a few k bits).
static constexpr long long kMaxIntTypeWidth = upass::kMaxIntTypeWidth;  // upass/core/range_bits.hpp -- one ceiling

// Parse the <N> in `u<N>/s<N>/i<N>`. Returns nullopt on non-numeric, overflow,
// negative, or above kMaxIntTypeWidth.
static std::optional<int> try_parse_int_type_width(std::string_view digits) {
  if (digits.empty()) {
    return std::nullopt;
  }
  size_t    pos = 0;
  long long v   = 0;
  try {
    v = std::stoll(std::string(digits), &pos);
  } catch (...) {
    return std::nullopt;
  }
  if (pos != digits.size() || v < 0 || v > kMaxIntTypeWidth) {
    return std::nullopt;
  }
  return static_cast<int>(v);
}

std::optional<std::pair<Dlop, Dlop>> Prp2lnast::folded_int_type_range(TSNode ty) const {
  if (ts_node_is_null(ty)) {
    return std::nullopt;
  }
  const std::string_view t(ts_node_type(ty));
  if (t != "uint_type" && t != "sint_type") {
    return std::nullopt;
  }
  TSNode           constraint = child_by_field(ty, "constraint");
  std::string_view kw         = trim(get_text(ty));
  if (!ts_node_is_null(constraint)) {
    kw = trim(kw.substr(0, kw.find('(')));
  }
  // `U<N>`/`S<N>` (sized) or `Unsigned`/`Signed` (unsized; sign from the node kind).
  const bool sized = kw.size() >= 2 && (kw[0] == 'U' || kw[0] == 'S')
                     && std::all_of(kw.begin() + 1, kw.end(), [](unsigned char ch) { return std::isdigit(ch); });
  const bool          is_signed = t == "sint_type";
  std::optional<Dlop> mx;
  std::optional<Dlop> mn;
  const auto          from_bits = [&](int64_t w) {
    if (w <= 0 || w > kMaxIntTypeWidth) {
      return false;
    }
    const auto n = static_cast<int>(w);
    mx           = is_signed ? *Dlop::get_mask_value(n - 1) : *Dlop::get_mask_value(n);
    mn           = is_signed ? *Dlop::get_neg_mask_value(n - 1) : *Dlop::create_integer(0);
    return true;
  };
  if (sized) {
    const auto w = try_parse_int_type_width(kw.substr(1));
    if (!w || !from_bits(*w)) {
      return std::nullopt;
    }
  } else if (!is_signed) {
    mn = *Dlop::create_integer(0);  // `Unsigned`
  }
  if (!ts_node_is_null(constraint)) {
    for (TSNode item : ts_node_named_children(constraint)) {
      const std::string_view it(ts_node_type(item));
      if (it != "assignment" && it != "arg_assignment" && it != "attribute_assignment") {
        continue;
      }
      TSNode lv = child_by_field(item, "lvalue");
      TSNode rv = child_by_field(item, "rvalue");
      if (ts_node_is_null(lv) || ts_node_is_null(rv)) {
        return std::nullopt;
      }
      const auto key = trim(get_text(lv));
      const auto v   = resolve_type_int_value(rv);
      if (!v) {
        return std::nullopt;
      }
      if (key == "bits") {
        if (!v->is_just_i64() || !from_bits(v->to_just_i64())) {
          return std::nullopt;
        }
      } else if (key == "max") {
        mx = *v;
      } else if (key == "min") {
        mn = *v;
      } else {
        return std::nullopt;
      }
    }
  }
  if (!mx || !mn) {
    return std::nullopt;
  }
  return std::pair{*mx, *mn};
}

Lnast_node Prp2lnast::emit_bound_binop(Lnast_ntype::Lnast_ntype_int head, const Lnast_node& l, const Lnast_node& r) {
  auto idx = builder.add_child(head);
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, l);
  lnast->add_child(idx, r);
  return ref;
}

// The range operator of an array dimension written as an index range
// (`[100..<132]`, 08-memories.md "Array index"): `op_range_exclusive` /
// `op_range_inclusive` / `op_range_count`, or "" for any other expression.
static std::string_view range_dim_op(TSNode e) {
  if (ts_node_is_null(e) || std::string_view(ts_node_type(e)) != "expression_item" || ts_node_named_child_count(e) != 3) {
    return {};
  }
  TSNode                 op = ts_node_named_child(e, 1);
  TSNode                 k  = std::string_view(ts_node_type(op)) == "binary_other_op" ? ts_node_named_child(op, 0) : TSNode{};
  const std::string_view kind(ts_node_is_null(k) ? "" : ts_node_type(k));
  return kind.starts_with("op_range_") ? kind : std::string_view{};
}

void Prp2lnast::prelower_type_bounds(TSNode type_cast_node) {
  if (ts_node_is_null(type_cast_node)) {
    return;
  }
  prelower_int_type_bounds(child_by_field(type_cast_node, "type"));
}

void Prp2lnast::prelower_int_type_bounds(TSNode ty) {
  if (ts_node_is_null(ty)) {
    return;
  }
  const std::string_view t(ts_node_type(ty));
  if (t == "array_type") {
    // `[N+1]unsigned(bits=N)`: the element bound defers like a scalar one, and
    // so does a dimension written as an expression over a generic (a bare name
    // keeps its text: the runner folds it, a generic one at specialization).
    TSNode len = child_by_field(ty, "length");
    TSNode idx = ts_node_is_null(len) ? TSNode{} : child_by_field(len, "index");
    if (TSNode e = prelowerable_array_dim(len); !ts_node_is_null(e)) {
      prelowered_array_dims_.insert_or_assign(ts_node_start_byte(len), expr_to_node(e));
    } else if (!range_dim_op(idx).empty()
               && !(resolve_type_int_value(ts_node_named_child(idx, 0)) && resolve_type_int_value(ts_node_named_child(idx, 2)))) {
      // An index range whose bounds do not fold here (`[N..<(N+2)]` over a
      // generic) lowers to a `range` value the runner reads once it folds.
      prelowered_array_dims_.insert_or_assign(ts_node_start_byte(len), expr_to_node(idx));
    }
    prelower_int_type_bounds(child_by_field(ty, "base"));
    return;
  }
  if (t != "uint_type" && t != "sint_type") {
    return;
  }
  TSNode constraint = child_by_field(ty, "constraint");
  if (ts_node_is_null(constraint)) {
    return;
  }
  prelower_visited_.insert(ts_node_start_byte(constraint));
  std::string kw(trim(get_text(ty)));
  if (const auto paren = kw.find('('); paren != std::string::npos) {
    kw = std::string(trim(std::string_view(kw).substr(0, paren)));
  }
  const bool is_signed = t == "sint_type";  // `S<N>` / `Signed`

  Prelowered_bounds out;
  for (TSNode item : ts_node_named_children(constraint)) {
    const std::string_view it(ts_node_type(item));
    if (it != "assignment" && it != "arg_assignment" && it != "attribute_assignment") {
      continue;
    }
    TSNode lv = child_by_field(item, "lvalue");
    TSNode rv = child_by_field(item, "rvalue");
    if (ts_node_is_null(lv) || ts_node_is_null(rv)) {
      continue;
    }
    const std::string key(trim(get_text(lv)));
    if (key != "bits" && key != "max" && key != "min") {
      continue;  // `range=` is rejected in int_type_call_bounds
    }
    if (resolve_type_int_value(rv)) {
      continue;  // folds here; nothing to defer
    }
    if (key == "max") {
      out.max = expr_to_node(rv);
      continue;
    }
    if (key == "min") {
      out.min = expr_to_node(rv);
      continue;
    }
    // `bits=E` is sugar (04b-attributes.md: "`bits` and `sign` are \"syntax
    // sugar\" translated from `max`/`min`"), so emit that translation:
    //   unsigned: max = (1 << E) - 1,        min = 0
    //   signed:   max = (1 << (E-1)) - 1,    min = 0 - (1 << (E-1))
    // The uniform formula reproduces the literal path's s1={-1,0} /
    // s2={-2..1} results exactly.
    const Lnast_node one   = Lnast_node::create_const("1");
    Lnast_node       e     = expr_to_node(rv);
    Lnast_node       width = is_signed ? emit_bound_binop(Lnast_ntype::create_minus(), e, one) : e;
    Lnast_node       span  = emit_bound_binop(Lnast_ntype::create_shl(), one, width);
    out.max                = emit_bound_binop(Lnast_ntype::create_minus(), span, one);
    out.min                = is_signed ? emit_bound_binop(Lnast_ntype::create_minus(), Lnast_node::create_const("0"), span)
                                       : Lnast_node::create_const("0");
  }
  if (!out.max.is_invalid() || !out.min.is_invalid()) {
    prelowered_int_bounds_.insert_or_assign(ts_node_start_byte(constraint), out);
  }
}

TSNode Prp2lnast::array_elem_type(TSNode ty) const {
  while (!ts_node_is_null(ty) && std::string_view(ts_node_type(ty)) == "array_type") {
    ty = child_by_field(ty, "base");
  }
  return ty;
}

TSNode Prp2lnast::prelowerable_array_dim(TSNode len) const {
  TSNode e = ts_node_is_null(len) ? TSNode{} : child_by_field(len, "index");
  if (ts_node_is_null(e) || std::string_view(ts_node_type(e)) == "identifier" || resolve_type_int_value(e)) {
    return {};
  }
  // A range dimension (`[0..<4]`, 08-memories.md) is not an extent.
  if (!range_dim_op(e).empty()) {
    return {};
  }
  return e;
}

Lnast_node Prp2lnast::array_dim_to_node(TSNode len) {
  if (const auto it = prelowered_array_dims_.find(ts_node_start_byte(len)); it != prelowered_array_dims_.end()) {
    return it->second;
  }
  // An expression that folds here (`[N+1]` over a visible `comptime const N`)
  // is written as its extent; every downstream reader parses `[n]`.
  TSNode e = child_by_field(len, "index");
  if (!ts_node_is_null(e) && std::string_view(ts_node_type(e)) != "identifier") {
    if (const auto v = resolve_type_int_value(e); v && v->is_just_i64() && v->to_just_i64() >= 0) {
      return Lnast_node::create_const(std::format("[{}]", v->to_just_i64()));
    }
    // An index range (`[100..<132]`, 08-memories.md "Array index") whose
    // bounds fold here is written with literal bounds; the runner lowers it to
    // its extent (upass::array_dim_range). One whose bounds do not fold was
    // prelowered to a `range` value (prelower_int_type_bounds); anywhere else
    // it keeps its text for the runner to fold by name.
    if (const auto kind = range_dim_op(e); !kind.empty()) {
      const auto lo = resolve_type_int_value(ts_node_named_child(e, 0));
      const auto hi = lo ? resolve_type_int_value(ts_node_named_child(e, 2)) : std::nullopt;
      if (lo && hi && lo->is_just_i64() && hi->is_just_i64()) {
        const char* spelling = kind == "op_range_inclusive" ? "..=" : (kind == "op_range_count" ? "..+" : "..<");
        return Lnast_node::create_const(std::format("[{}{}{}]", lo->to_just_i64(), spelling, hi->to_just_i64()));
      }
    }
  }
  return expr_to_node(len);
}

// Does this integer type (or array element type) carry a `bits=`/`max=`/`min=`
// bound that does not fold here (a generic parameter, an expression over one)?
bool Prp2lnast::int_type_has_unfoldable_bound(TSNode ty) const {
  ty = array_elem_type(ty);
  if (ts_node_is_null(ty)) {
    return false;
  }
  const std::string_view t(ts_node_type(ty));
  if (t != "uint_type" && t != "sint_type") {
    return false;
  }
  TSNode constraint = child_by_field(ty, "constraint");
  if (ts_node_is_null(constraint)) {
    return false;
  }
  for (TSNode item : ts_node_named_children(constraint)) {
    const std::string_view it(ts_node_type(item));
    if (it != "assignment" && it != "arg_assignment" && it != "attribute_assignment") {
      continue;
    }
    TSNode lv = child_by_field(item, "lvalue");
    TSNode rv = child_by_field(item, "rvalue");
    if (ts_node_is_null(lv) || ts_node_is_null(rv)) {
      continue;
    }
    const std::string key(trim(get_text(lv)));
    if ((key == "bits" || key == "max" || key == "min") && !resolve_type_int_value(rv)) {
      return true;
    }
  }
  return false;
}

// 2f-generic_port_width — a port (or port tuple field) type of a GENERIC
// lambda whose integer bound does not fold here: the placeholder entry makes
// int_type_call_bounds treat the site as deferred (no warning) and
// emit_type_expr emit `nil` for now; flush_deferred_port_bounds rewrites the
// leaves of `store` once the body prologue exists.
void Prp2lnast::defer_port_bound(TSNode type_cast, const Lnast_nid& store) {
  if (ts_node_is_null(type_cast)) {
    return;
  }
  TSNode ty           = child_by_field(type_cast, "type");
  // An array port's dimensions: a name of a visible `comptime const`
  // (`v:[W]u4`) folds here -- outside a template nothing downstream folds a
  // port's shape. An expression over a generic (`v:[N+1]u4`) defers like an
  // integer bound: a placeholder now, the body-prologue ref once flushed.
  bool   deferred_dim = false;
  for (TSNode at = ty; std::string_view(ts_node_type(at)) == "array_type"; at = child_by_field(at, "base")) {
    TSNode len = child_by_field(at, "length");
    TSNode e   = ts_node_is_null(len) ? TSNode{} : child_by_field(len, "index");
    if (ts_node_is_null(e)) {
      continue;
    }
    if (std::string_view(ts_node_type(e)) == "identifier") {
      if (const auto v = resolve_type_int_value(e); v && v->is_just_i64() && v->to_just_i64() >= 0) {
        prelowered_array_dims_.insert_or_assign(ts_node_start_byte(len),
                                                Lnast_node::create_const(std::format("[{}]", v->to_just_i64())));
      }
    } else if (lambda_has_generics_ && !ts_node_is_null(prelowerable_array_dim(len))) {
      prelowered_array_dims_.try_emplace(ts_node_start_byte(len), Lnast_node::create_const("nil"));
      deferred_dim = true;
    }
  }
  const bool deferred_bound = lambda_has_generics_ && int_type_has_unfoldable_bound(ty);
  if (deferred_bound) {
    prelowered_int_bounds_.try_emplace(ts_node_start_byte(child_by_field(array_elem_type(ty), "constraint")), Prelowered_bounds{});
  }
  if (deferred_bound || deferred_dim) {
    pending_port_bounds_.push_back({type_cast, store});
  }
}

// 2f-generic_port_width — see Pending_port_bound. Runs once the body stmts
// frame is open: the desugar lands in the body prologue and the io leaves are
// rewritten IN PLACE (the tree is append-only, and SSA's flatten_assign takes
// the FIRST type child, so appending a second prim_type_int would not do).
void Prp2lnast::flush_deferred_port_bounds() {
  auto pending = std::exchange(pending_port_bounds_, {});
  auto patch   = [&](const Lnast_nid& leaf, const Lnast_node& n) {
    if (leaf.is_invalid() || n.is_invalid()) {
      return;
    }
    lnast->set_type(leaf, n.get_type());
    lnast->set_name(leaf, n.get_name());
  };
  for (const auto& pb : pending) {
    prelower_type_bounds(pb.type_cast);  // emits into the current stmts frame == the body prologue
    // A deferred dimension: comp_type_array(elem, dim) mirrors the array_type
    // levels, outermost first.
    TSNode at = child_by_field(pb.type_cast, "type");
    for (auto c : lnast->children(pb.store)) {
      for (auto lvl = c; !lvl.is_invalid() && Lnast_ntype::is_comp_type_array(lnast->get_type(lvl)) && !ts_node_is_null(at)
                         && std::string_view(ts_node_type(at)) == "array_type";
           lvl = lnast->get_first_child(lvl), at = child_by_field(at, "base")) {
        const auto elem = lnast->get_first_child(lvl);
        TSNode     len  = child_by_field(at, "length");
        const auto it = ts_node_is_null(len) ? prelowered_array_dims_.end() : prelowered_array_dims_.find(ts_node_start_byte(len));
        if (!elem.is_invalid() && it != prelowered_array_dims_.end() && it->second.is_ref()) {
          patch(lnast->get_sibling_next(elem), it->second);
        }
      }
    }
    TSNode ty         = array_elem_type(child_by_field(pb.type_cast, "type"));
    TSNode constraint = ts_node_is_null(ty) ? TSNode{} : child_by_field(ty, "constraint");
    if (ts_node_is_null(constraint)) {
      continue;
    }
    const auto it = prelowered_int_bounds_.find(ts_node_start_byte(constraint));
    if (it == prelowered_int_bounds_.end()) {
      continue;
    }
    for (auto c : lnast->children(pb.store)) {
      // An array port's element sits under its comp_type_array levels.
      while (Lnast_ntype::is_comp_type_array(lnast->get_type(c)) && !lnast->get_first_child(c).is_invalid()) {
        c = lnast->get_first_child(c);
      }
      if (!Lnast_ntype::is_prim_type_int(lnast->get_type(c))) {
        continue;
      }
      auto mx = lnast->get_first_child(c);
      auto mn = mx.is_invalid() ? mx : lnast->get_sibling_next(mx);
      patch(mx, it->second.max);
      patch(mn, it->second.min);
      break;
    }
  }
}

bool Prp2lnast::int_type_call_bounds(std::string_view kw, TSNode tup, std::string& max_txt, std::string& min_txt) {
  // Classify the base keyword → signedness and optional concrete width.
  // `Unsigned`/`Signed` or the sized `U<N>`/`S<N>` (docs 07-typesystem).
  bool unsigned_base = false;
  bool signed_base   = false;
  if (kw == "Unsigned") {
    unsigned_base = true;
  } else if (kw == "Signed") {
    signed_base = true;
  } else if (kw.size() >= 2 && kw[0] == 'U'
             && std::all_of(kw.begin() + 1, kw.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
    unsigned_base = true;
  } else if (kw.size() >= 2 && kw[0] == 'S'
             && std::all_of(kw.begin() + 1, kw.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
    signed_base = true;
  } else {
    return false;
  }
  auto bits_to_bounds = [&](int n, std::string& maxt, std::string& mint) {
    if (signed_base) {
      // s1 is {-1,0}, s2 is {-2..1}: 2^(n-1)-1 down to -2^(n-1).
      maxt = std::string(Dlop::get_mask_value(n - 1)->to_pyrope());
      mint = std::string(Dlop::get_neg_mask_value(n - 1)->to_pyrope());
    } else {
      maxt = std::string(Dlop::get_mask_value(n)->to_pyrope());
      mint = "0";
    }
  };
  if (unsigned_base) {
    min_txt = "0";
  }
  // Concrete width sugar (`U8(min=…)`): seed bounds from the width first.
  if (kw.size() >= 2 && (kw[0] == 'U' || kw[0] == 'S') && std::isdigit(static_cast<unsigned char>(kw[1]))) {
    auto w = try_parse_int_type_width(kw.substr(1));
    if (!w) {
      report_error(tup,
                   "width-too-large",
                   "type",
                   std::format("integer type width '{}' is out of range (0..{} bits)", kw, kMaxIntTypeWidth),
                   "use a smaller bit width");
    }
    bits_to_bounds(*w, max_txt, min_txt);
  }
  // Refine with the named args.
  for (TSNode item : ts_node_named_children(tup)) {
    std::string_view it(ts_node_type(item));
    if (it != "assignment" && it != "arg_assignment" && it != "attribute_assignment") {
      continue;
    }
    TSNode lv = child_by_field(item, "lvalue");
    TSNode rv = child_by_field(item, "rvalue");
    if (ts_node_is_null(lv) || ts_node_is_null(rv)) {
      continue;
    }
    std::string key(trim(get_text(lv)));
    std::string val(trim(get_text(rv)));
    const auto  folded_value = resolve_type_int_value(rv);
    const bool  folded       = folded_value.has_value();
    if (folded) {
      val = std::string(folded_value->to_pyrope());
    }
    auto bound_not_comptime
        = [&](std::string_view which) { return std::format("`{}(...)` bound `{}` is not a compile-time value", kw, which); };
    // A generic parameter only works where prelower_type_bounds runs -- a
    // VARIABLE DECLARATION inside the body, or a port/return type of the
    // generic lambda itself (flush_deferred_port_bounds). A tuple field type or
    // a `f<signed(bits=N)>` generic argument has no statement position to
    // desugar the bound into, so say so instead of promising a spelling that
    // will land back here.
    constexpr std::string_view bound_hint
        = "use a literal or a `comptime const`; a generic parameter is resolved on a variable declaration inside the body "
          "and on the ports of the generic lambda itself (`mod m<N=8>(a:Unsigned(bits=N))`), not on a tuple field type or "
          "a `<…>` argument";
    if (key == "range") {
      // No `range` type argument — it would be pure sugar for max/min, so it
      // is rejected to keep a single way to bound an integer type.
      report_error(item,
                   "int-type-range",
                   "type",
                   std::format("`{}(range=…)` is not a valid integer type bound", kw),
                   "bound the type with `max=`/`min=` (e.g. `Unsigned(max=15, min=0)`) or a width type `U4`/`S5`");
    }
    const bool deferred         = prelowered_int_bounds_.contains(ts_node_start_byte(tup));
    // Reached by prelower_type_bounds? If not, this site (a port/return type, a
    // tuple field type, a `f<signed(bits=N)>` generic argument) has no
    // statement position to desugar into, so an unfoldable bound cannot be
    // deferred -- and it must NOT be a hard error either: that shape compiled
    // before deferral existed. Warn and fall back to the previous unbounded
    // lowering, so the drop is at least no longer SILENT.
    const bool prelowered_here  = prelower_visited_.contains(ts_node_start_byte(tup));
    auto       bound_unresolved = [&](std::string_view which) {
      // A name the bound reads must still be declared: this site does not lower
      // the bound, so nothing else records the read.
      if (!prelowered_here) {
        record_name_reads(rv);
        // A declared RUNTIME value (an input, a const computed from one) never
        // sizes a type: an error, not the dropped-width warning below.
        for (TSNode c : expr_reads(rv, /*enter_attribute_reads=*/false).names) {
          const std::string nm{canonical_escaped_ident(trim(get_text(c)))};
          if (const auto hit = lookup_capture(nm); hit && hit->kind == Bind_kind::runtime && !hit->binding->int_value) {
            if (hit->binding->fold_const) {
              // A plain `const` may still fold to a constant (user ruling
              // 2026-09-28 (32)), but not one this declaration can evaluate.
              report_error(c,
                           "type-bound-not-comptime",
                           "type",
                           std::format("`{}(...)` bound `{}` reads const `{}`, whose value this declaration cannot "
                                             "evaluate (it is computed by statements -- a loop, an `if`, a call -- or from a "
                                             "runtime value)",
                                       kw,
                                       which,
                                       nm),
                           "size it with a literal, a generic parameter, or a const folded from literals (`const W = N + 1`)");
            }
            report_error(c,
                         "type-bound-not-comptime",
                         "type",
                         std::format("`{}(...)` bound `{}` reads `{}`, a runtime value: a type bound must be a compile-time "
                                           "value",
                                     kw,
                                     which,
                                     nm),
                         "size it with a literal, a generic parameter, or a `const` with a compile-time value");
          }
        }
      }
      if (const auto cpt = unfolded_comptime_read(rv)) {
        report_error(rv,
                     "type-bound-not-comptime",
                     "type",
                     std::format("`{}(...)` bound `{}` reads comptime const `{}`, whose value is computed by statements (a "
                                       "loop, an `if`, a call) this declaration cannot evaluate",
                                 kw,
                                 which,
                                 *cpt),
                     "size it with a literal, a generic parameter, or a comptime const folded from literals (`comptime "
                           "const W = N + 1`)");
      }
      if (prelowered_here) {
        // A site prelowering DID visit: the bound is genuinely not comptime, so
        // this is a hard error. Return -- without it the warning below fired too
        // and the same defect was reported twice under one code.
        report_error(rv, "type-bound-not-comptime", "type", bound_not_comptime(which), bound_hint);
        return;
      }
      report_warning(rv, "type-bound-not-comptime", "type", bound_not_comptime(which), bound_hint);
    };
    if (key == "max") {
      // 2f-type_bound — `val` defaults to the RAW SOURCE TEXT, so an unfoldable
      // bound used to be written into a `const` node verbatim. Never do that:
      // either it folded, or it was deferred as a ref, or it is an error.
      if (folded) {
        max_txt = val;
      } else if (!deferred) {
        bound_unresolved(key);
      }
    } else if (key == "min") {
      if (folded) {
        min_txt = val;
      } else if (!deferred) {
        bound_unresolved(key);
      }
    } else if (key == "bits") {
      auto bv = resolve_type_int_value(rv);
      if (bv && bv->is_just_i64()) {
        const auto bits = bv->to_just_i64();
        if (bits < 0 || bits > kMaxIntTypeWidth) {
          report_error(rv,
                       "width-too-large",
                       "type",
                       std::format("integer type width '{}' is out of range (0..{} bits)", val, kMaxIntTypeWidth),
                       "use a smaller bit width");
        }
        bits_to_bounds(static_cast<int>(bits), max_txt, min_txt);
      } else if (!deferred) {
        // The arm used to be a silent no-op here: `max_txt` stayed empty, the
        // declare got `prim_type_int(nil, 0)`, and the register was sized by
        // whatever value happened to reach it. Exit 0, wrong hardware.
        bound_unresolved(key);
      }
    }
  }
  return true;
}

std::optional<Dlop> Prp2lnast::resolve_type_int_value(TSNode n) const {
  if (ts_node_is_null(n)) {
    return std::nullopt;
  }

  const std::string_view t(ts_node_type(n));
  if (t == "constant") {
    auto v = Dlop::from_pyrope(trim(get_text(n)));
    if (v && v->is_integer() && !v->has_unknowns()) {
      return *v;
    }
    return std::nullopt;
  }
  if (t == "identifier") {
    if (const auto v = visible_const_int(n)) {
      return *Dlop::create_integer(*v);
    }
    return std::nullopt;
  }
  if ((t == "tuple" || t == "expression_type" || t == "paren_group" || t == "expression_list")
      && ts_node_named_child_count(n) == 1) {
    return resolve_type_int_value(ts_node_named_child(n, 0));
  }
  // A field of a file-scope comptime tuple (`cfg.w`, parsed as a dotted type
  // name): fold the field's initializer.
  if (t == "expression_type" && ts_node_named_child_count(n) == 2
      && std::string_view(ts_node_type(ts_node_named_child(n, 0))) == "identifier"
      && std::string_view(ts_node_type(ts_node_named_child(n, 1))) == "identifier") {
    TSNode     base_id = ts_node_named_child(n, 0);
    const auto base    = canonical_escaped_ident(trim(get_text(base_id)));
    const auto field   = trim(get_text(ts_node_named_child(n, 1)));
    check_capture_read(base, base_id);
    // const_rvalue_nodes_ only holds file-scope declarations (frame 0).
    const auto hit = lookup_capture(base);
    const auto tup = const_rvalue_nodes_.find(base);
    if (!hit || hit->kind != Bind_kind::comptime || hit->frame != 0 || tup == const_rvalue_nodes_.end()
        || std::string_view(ts_node_type(tup->second)) != "tuple") {
      return std::nullopt;
    }
    for (TSNode item : ts_node_named_children(tup->second)) {
      TSNode lv = child_by_field(item, "lvalue");
      if (std::string_view(ts_node_type(item)) != "assignment" || ts_node_is_null(lv)) {
        continue;
      }
      if (TSNode fid = std::string_view(ts_node_type(lv)) == "typed_identifier" ? child_by_field(lv, "identifier") : lv;
          !ts_node_is_null(fid) && trim(get_text(fid)) == field) {
        return resolve_type_int_value(child_by_field(item, "rvalue"));
      }
    }
    return std::nullopt;
  }
  // An integer attribute read (`Z.[bits]`, `Z.[max]`, `Z.[min]`) of a visible
  // binding declared with an integer type whose bounds fold: the attribute
  // comes from the TYPE (see Binding::range).
  if (t == "attribute_read") {
    TSNode arg = child_by_field(n, "argument");
    if (ts_node_is_null(arg) || ts_node_named_child_count(n) != 2) {
      return std::nullopt;
    }
    auto attr = trim(get_text(ts_node_named_child(n, 1)));
    if (attr.size() < 2 || attr.front() != '[' || attr.back() != ']') {
      return std::nullopt;
    }
    attr = trim(attr.substr(1, attr.size() - 2));
    if (std::string_view(ts_node_type(arg)) == "constant") {
      // A literal's `.[bits]` is value-derived like an untyped comptime
      // integer's (user rulings 2026-09-27 (6) and 2026-09-28 (23)): 13 -> 4.
      const auto v = attr == "bits" ? resolve_type_int_value(arg) : std::nullopt;
      return v ? std::optional<Dlop>(*Dlop::create_integer(upass::value_bits(*v))) : std::nullopt;
    }
    if (std::string_view(ts_node_type(arg)) != "identifier") {
      return std::nullopt;
    }
    const auto name = canonical_escaped_ident(trim(get_text(arg)));
    check_capture_read(name, arg);
    const auto hit = lookup_capture(name);
    // An attribute read is comptime whatever it reads (ruling 2026-09-28
    // (23)): a typed runtime binding (`a:u8` input, `mut x:u4`) states it.
    if (!hit || (hit->kind != Bind_kind::comptime && !(hit->binding->typed && hit->binding->range))) {
      return std::nullopt;
    }
    if (!hit->binding->range) {
      // An UNTYPED comptime integer is as wide as its value (user ruling
      // 2026-09-27 (6), the rule upass.attributes applies): 13 -> 4, 0 -> 1.
      // A typed one keeps its declared type even unbounded (`Z:unsigned`
      // reads nil), so it never folds from the value.
      if (attr != "bits" || hit->binding->typed || !hit->binding->int_value) {
        return std::nullopt;
      }
      return *Dlop::create_integer(upass::value_bits(*Dlop::create_integer(*hit->binding->int_value)));
    }
    const auto& [mx, mn] = *hit->binding->range;
    if (attr == "max") {
      return mx;
    }
    if (attr == "min") {
      return mn;
    }
    if (attr == "bits") {
      return *Dlop::create_integer(upass::range_bits(mx, mn));
    }
    return std::nullopt;
  }
  if (t == "tuple" || t == "expression_type") {
    return std::nullopt;
  }
  // `std.clog2(x)` of a foldable x (docs 13-stdlib): a comptime width, like the
  // arithmetic below. A bad x (<= 0) does not fold here; upass reports it.
  if (t == "function_call_expression") {
    TSNode fn  = child_by_field(n, "function");
    TSNode arg = child_by_field(n, "argument");
    if (ts_node_is_null(fn) || ts_node_is_null(arg) || std::string_view(ts_node_type(fn)) != "dot_expression"
        || ts_node_named_child_count(fn) != 2 || trim(get_text(ts_node_named_child(fn, 0))) != prp_builtins::std_namespace
        || trim(get_text(ts_node_named_child(fn, 1))) != "clog2" || ts_node_named_child_count(arg) != 1
        || std::string_view(ts_node_type(ts_node_named_child(arg, 0))) == "assignment") {
      return std::nullopt;
    }
    if (const auto v = resolve_type_int_value(ts_node_named_child(arg, 0))) {
      return upass::std_clog2(*v);
    }
    return std::nullopt;
  }
  if (t == "unary_expression") {
    TSNode op  = child_by_field(n, "operator");
    TSNode arg = child_by_field(n, "argument");
    auto   v   = resolve_type_int_value(arg);
    if (!v || ts_node_is_null(op)) {
      return std::nullopt;
    }
    const std::string_view kind(ts_node_type(op));
    if (kind == "op_unary_minus") {
      return *Dlop::create_integer(0)->sub_op(*v);
    }
    if (kind == "op_unary_plus") {
      return v;
    }
    return std::nullopt;
  }
  if (t != "expression_item") {
    return std::nullopt;
  }

  std::vector<Dlop>             values;
  std::vector<std::string_view> ops;
  for (TSNode c : ts_node_named_children(n)) {
    const std::string_view ct(ts_node_type(c));
    if (ct == "binary_times_op" || ct == "binary_other_op") {
      TSNode inner = ts_node_named_child(c, 0);
      if (ts_node_is_null(inner)) {
        return std::nullopt;
      }
      ops.emplace_back(ts_node_type(inner));
    } else {
      auto v = resolve_type_int_value(c);
      if (!v) {
        return std::nullopt;
      }
      values.emplace_back(std::move(*v));
    }
  }
  if (values.empty() || values.size() != ops.size() + 1) {
    return std::nullopt;
  }

  Dlop result = values.front();
  for (std::size_t i = 0; i < ops.size(); ++i) {
    const auto& rhs = values[i + 1];
    if (ops[i] == "op_add") {
      result = result.add_op(rhs);
    } else if (ops[i] == "op_sub") {
      result = result.sub_op(rhs);
    } else if (ops[i] == "op_mul") {
      result = result.mult_op(rhs);
    } else if (ops[i] == "op_shl") {
      result = result.shl_op(rhs);
    } else if (ops[i] == "op_sra") {
      result = result.sra_op(rhs);
    } else {
      return std::nullopt;
    }
    if (!result.is_integer() || result.has_unknowns()) {
      return std::nullopt;
    }
  }
  return result;
}

bool Prp2lnast::is_prim_type_token(std::string_view txt) {
  // A bare built-in type word (`U8`, `S4`, `Unsigned`, `Signed`, `Bool`,
  // `String`, `Clock`, `Reset`). A backticked `` `U8` `` keeps its backticks
  // through get_text, so it is a plain name here.
  return str_tools::is_pyrope_type_word(txt);
}

// An integer TYPE literal written as a call -- only the named-size form
// (`Unsigned(bits=4)`, `S8(min=-3)`): at least one argument, all `max=` /
// `min=` / `bits=` (`U8(3)` stays a cast). Declares a type tmp carrying the
// canonical prim_type_int(max, min), read back through the attributes
// Type_info seam like a declared variable (`Unsigned(bits=4).[max]`,
// `x does Unsigned(bits=4)`). nullopt for any other call.
std::optional<Lnast_node> Prp2lnast::int_type_literal(TSNode n) {
  TSNode fn  = child_by_field(n, "function");
  TSNode arg = child_by_field(n, "argument");
  if (ts_node_is_null(fn) || ts_node_is_null(arg) || !is_prim_type_token(trim(get_text(fn)))
      || (std::string_view(ts_node_type(arg)) != "tuple" && std::string_view(ts_node_type(arg)) != "arg_tuple")) {
    return std::nullopt;
  }
  bool all_size = ts_node_named_child_count(arg) >= 1;
  for (TSNode item : ts_node_named_children(arg)) {
    if (!all_size) {
      break;
    }
    std::string_view it(ts_node_type(item));
    if (it != "assignment" && it != "arg_assignment" && it != "attribute_assignment") {
      all_size = false;
      break;
    }
    TSNode lv = child_by_field(item, "lvalue");
    if (ts_node_is_null(lv)) {
      all_size = false;
      break;
    }
    auto key = trim(get_text(lv));
    all_size = (key == "max" || key == "min" || key == "bits");
  }
  std::string max_txt;
  std::string min_txt;
  if (!all_size || !int_type_call_bounds(trim(get_text(fn)), arg, max_txt, min_txt)) {
    return std::nullopt;
  }
  auto didx = builder.add_child(Lnast_ntype::create_declare());
  auto ref  = builder.mint_tmp_ref();
  lnast->add_child(didx, ref);
  auto pt = lnast->add_child(didx, Lnast_ntype::create_prim_type_int());
  lnast->add_child(pt, Lnast_node::create_const(max_txt.empty() ? "nil" : max_txt));
  lnast->add_child(pt, Lnast_node::create_const(min_txt.empty() ? "nil" : min_txt));
  lnast->add_child(didx, Lnast_node::create_const("type"));
  return ref;
}

Lnast_node Prp2lnast::does_operand_to_node(TSNode n) {
  std::string_view t(ts_node_type(n));
  if (t == "identifier") {
    auto name = trim(get_text(n));
    if (!lookup_capture(canonical_escaped_ident(name))) {
      check_type_name_spelling(n);  // `x does I8`, unless a value is named so
    }
    if (is_prim_type_token(name)) {
      // Type-token operand (`a does U32`): a bare ref with NO read site —
      // check_undefined_reads must not demand a variable here. constprop
      // decodes the name to kind+envelope. A type word is reserved, so no
      // variable can be named so (a backticked `` `U32` `` keeps its backticks).
      return Lnast_node::create_ref(name);
    }
  } else if (t == "function_call_expression") {
    if (auto lit = int_type_literal(n)) {
      return *lit;
    }
  }
  return expr_to_node(n);
}

// 2f-nested_type — the inner `tuple` node when this type cast's type is a tuple
// SHAPE (`:(a:u1, b:u1)`), else a null node. Used to recurse into a nested
// tuple-typed FIELD instead of re-entering emit_type_spec, which would emit a
// statement-level store against a bare field name.
TSNode Prp2lnast::tuple_type_inner(TSNode type_cast_node) const {
  if (ts_node_is_null(type_cast_node)) {
    return TSNode{};
  }
  TSNode ty = child_by_field(type_cast_node, "type");
  if (ts_node_is_null(ty) || std::string_view(ts_node_type(ty)) != "expression_type") {
    return TSNode{};
  }
  for (TSNode inner : ts_node_named_children(ty)) {
    if (std::string_view(ts_node_type(inner)) == "tuple") {
      return inner;
    }
  }
  return TSNode{};
}

void Prp2lnast::emit_tuple_type_field_specs(std::string_view path, TSNode tuple_node) {
  size_t anon_pos = 0;  // `_:T` entries are stamped by position (`path.0`, `path.1`)
  for (TSNode item : ts_node_named_children(tuple_node)) {
    if (std::string_view(ts_node_type(item)) != "typed_field") {
      continue;
    }
    TSNode fid = child_by_field(item, "identifier");
    TSNode ftc = child_by_field(item, "type");
    if (ts_node_is_null(fid) || ts_node_is_null(ftc)) {
      continue;
    }
    const std::string fname
        = trim(get_text(fid)) == "_" ? std::to_string(anon_pos++) : std::string{canonical_escaped_ident(trim(get_text(fid)))};
    // The parser supplies one identifier here. A dot can only be inside an
    // escaped field name; retain its backticks in the qualified path.
    if (fname.empty()) {
      continue;
    }
    const std::string fpath = std::string(path) + "." + fname;
    // A field whose type is ITSELF a tuple: descend, stamping only the LEAVES.
    // Re-entering emit_type_spec here would emit `store(ref '<fpath>', %tmp)`
    // -- a statement-level store whose child-0 is a path that was never
    // declared -- which check_writes_in_scope reports as "assignment to
    // undeclared variable", with the span on the DECLARATION line. That is why
    // `reg ctl:(ex:(a:u1), wb:(c:u1))` could not be declared at all.
    if (TSNode nested = tuple_type_inner(ftc); !ts_node_is_null(nested)) {
      emit_tuple_type_field_specs(fpath, nested);
      continue;
    }
    emit_type_spec(Lnast_node::create_ref(fpath), ftc);
  }
}

void Prp2lnast::emit_type_spec(const Lnast_node& target, TSNode type_cast_node) {
  // type_cast: ':' + (type + optional attribute | attribute). The integer
  // type-call form (`int(max=3)`) parses as a uint_type/sint_type carrying a
  // `constraint` tuple; emit_type_expr folds the constraint into the
  // prim_type_int bounds.
  TSNode ty = child_by_field(type_cast_node, "type");
  if (!ts_node_is_null(ty)) {
    // Tuple-shape type with default values (e.g. `:(x=0, y=1)`) is the only
    // way to declare a named-position tuple in Pyrope. Lower the inner
    // tuple as a normal `tuple_add` and assign it to the target so the
    // bundle starts with `x=0, y=1` named keys; subsequent positional
    // assignments use upass's shape-preserving merge to keep those names.
    // The bit-width primitive types are still ignored at the value-folding
    // layer — this only handles the shape carried by the tuple type.
    std::string_view tt(ts_node_type(ty));
    if (tt == "expression_type") {
      for (TSNode inner : ts_node_named_children(ty)) {
        if (std::string_view(ts_node_type(inner)) == "tuple") {
          // A tuple used as a TYPE must have NAMED fields (`name:type`, parsed as
          // `typed_field`). A bare type entry like `(int, int, int)` has no field
          // name and is malformed — point the user at an array `[N]T` (uniform
          // elements) or a named field. (2f-arg_naming_tuple.)
          for (TSNode item : ts_node_named_children(inner)) {
            // A bare `identifier` item (`(int, int, int)`) is a TYPE with no field
            // name. Named fields (`x:int` → typed_field, `x=nil` → assignment) are
            // fine — only the unnamed bare-type form is malformed.
            if (std::string_view(ts_node_type(item)) == "identifier") {
              report_error(
                  item,
                  "unnamed-tuple-type-field",
                  "type",
                  std::format("a tuple-type field must be named (`name:T`): a bare `{}` has no field name", trim(get_text(item))),
                  "use an array type `[N]T` for N uniform elements, or name each field "
                  "(`_:T` for an anonymous field)");
            }
          }
          // Stamp the per-field types FIRST — before the shape seed itself.
          // The detupler learns a NESTED layout only from these dotted leaf
          // type_specs (the seed's own children are bare refs with no scalar
          // type of their own), and it decides whether to keep the declaration
          // pending when it SEES the seed. Emitted after it, the layout arrived
          // too late and the seed was flushed as undescribable.
          if (const auto tn = target.get_name(); !tn.empty()) {
            emit_tuple_type_field_specs(tn, inner);
          }
          auto bundle_ref = tuple_to_node(inner, false, /*field_types_on_target=*/true);
          auto aidx       = builder.add_child(Lnast_ntype::create_store());
          lnast->add_child(aidx, target);
          lnast->add_child(aidx, bundle_ref);
          // 2c-wire: this store is the TYPE's shape, not a driver. Record the
          // target so the single-driver counter discounts it (see the member).
          decl_shape_seed_stores_.insert(aidx);
          // The tuple-shape store above carries the field VALUES/shape, and
          // tuple_to_node records each field's type on the field's tuple_get
          // tmp (read side: `does`/`.[bits]`). ALSO stamp each field's type on
          // the DOTTED field path `target.field` so the runner's dotted-declare
          // bake records decl_max/min on the field entry (pending until the
          // value-store establishes the field). This is what lets the per-field
          // overflow check (cat 3) see `x.a`'s declared range — the tmp-keyed
          // form never lands on the field's value-bundle entry.

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
    // Preserve the typename when the type is an identifier-based (named)
    // type so `target.[typename]` reads can resolve at the attribute pass.
    if (tt == "expression_type" || tt == "dot_expression_type" || tt == "function_call_type") {
      auto raw = trim(get_text(ty));
      if (!raw.empty()) {
        auto as_idx = builder.add_child(Lnast_ntype::create_attr_set());
        lnast->add_child(as_idx, target);
        lnast->add_child(as_idx, Lnast_node::create_const("typename"));
        std::string quoted;
        quoted.reserve(raw.size() + 2);
        quoted.push_back('\'');
        quoted.append(raw);
        quoted.push_back('\'');
        lnast->add_child(as_idx, Lnast_node::create_const(quoted));
      }
    }
  }
attrs:
  uint32_t total = ts_node_child_count(type_cast_node);
  for (uint32_t i = 0; i < total; i++) {
    TSNode           c     = ts_node_child(type_cast_node, i);
    const char*      fname = ts_node_field_name_for_child(type_cast_node, i);
    std::string_view ct(ts_node_type(c));
    if (ct == "attribute_list") {
      emit_attribute_list(target, c);
    } else if ((ct == "tuple_sq" || ct == "attribute_sq") && fname && std::string_view(fname) == "attribute") {
      emit_attribute_list(target, c);
    }
  }
}

// A type position spelled `I<N>` (`I8`, `I32`): not a type word, so prpparse
// reads it as a named type. The signed sized type is `S<N>`; point there
// instead of the generic unknown-type error. Lowercase former type spellings
// are ordinary names, so check their lexical bindings before diagnosing them.
void Prp2lnast::check_type_name_spelling(const TSNode& node) const {
  const auto text = trim(get_text(node));
  if (const auto renamed = str_tools::renamed_type_spelling(text);
      !renamed.empty() && !lookup_capture(text) && !name_in_inflight_scope(text)
      && std::none_of(capture_frames_.begin(), capture_frames_.end(), [&](const Capture_frame& frame) {
           return frame.types.contains(text) || frame.lambdas.contains(text);
         })) {
    report_error(node,
                 "renamed-type",
                 "type",
                 std::format("`{}` was renamed `{}`", text, renamed),
                 std::format("use `{}` for the built-in type, or declare `{}` before using it as a name", renamed, text));
  }
  if (text.size() >= 2 && text[0] == 'I'
      && std::all_of(text.begin() + 1, text.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
    report_removed_int_type(span_of_node(node), text);
  }
}

void Prp2lnast::report_removed_int_type(livehd::diag::Span span, std::string_view text) const {
  const auto width = text.substr(1);
  stage_error(std::move(span),
              "removed-int-type",
              "syntax",
              std::format("`{}` was renamed `S{}`", text, width),
              std::format("write `S{}` (a signed {}-bit integer)", width, width));
}

void Prp2lnast::record_type_name_read(const TSNode& type_node) {
  // A named type is a read of the type SYMBOL: route it through the same
  // undefined-read check as value reads. Record only the LEADING identifier —
  // the symbol that must resolve — stripping any `.field` (dotted/import),
  // `(args)` (generic call), or trailing syntax, mirroring how a value read of
  // `a.b` only records the base `a`.
  auto   raw = trim(get_text(type_node));
  size_t n   = 0;
  while (n < raw.size()) {
    const unsigned char c = static_cast<unsigned char>(raw[n]);
    if (std::isalnum(c) || c == '_' || c == '$' || c == '%' || c == '#' || c == '`') {
      ++n;
    } else {
      break;
    }
  }
  const std::string base{canonical_escaped_ident(trim(raw.substr(0, n)))};
  if (base.empty() || name_in_inflight_scope(base)) {
    return;
  }
  // Validate type references in plain statement contexts (top-level and
  // lambda BODIES, where inflight_name_scopes_ is empty) and in the SIGNATURE
  // of a streamed lambda: its own generics (`comb f<T>(a:T)`), the enclosing
  // types and the file imports all sit in streamed_scope_names_, which
  // read_is_visible consults for a site with no stmts frame. A kept func_def's
  // signature or a tuple literal (the other non-empty stacks) is skipped: its
  // generics are not visible from the enclosing frame the site would record.
  const bool streamed_signature = is_streamed_signature();
  if (!inflight_name_scopes_.empty() && !streamed_signature) {
    return;
  }
  const auto sp = ts_node_start_point(type_node);
  read_sites_.push_back(Read_site{.name       = base,
                                  .start_byte = ts_node_start_byte(type_node),
                                  .end_byte   = ts_node_start_byte(type_node) + static_cast<uint32_t>(n),
                                  .start_line = sp.row + 1,
                                  .start_col  = sp.column + 1,
                                  .end_line   = sp.row + 1,
                                  .end_col    = sp.column + 1 + static_cast<uint32_t>(n),
                                  .scope      = streamed_signature ? Lnast_nid{} : builder.idx_stmts,
                                  .before     = streamed_signature ? Lnast_nid{} : lnast->get_last_child(builder.idx_stmts),
                                  .is_type    = true});
}

bool Prp2lnast::is_streamed_signature() const {
  return inflight_name_scopes_.size() == 1 && capture_frames_.back().lambda_boundary && capture_frames_.back().streamed;
}

void Prp2lnast::push_read_site(TSNode id, std::string name, bool no_frame, Generic_read generic) {
  const auto sp = ts_node_start_point(id);
  const auto ep = ts_node_end_point(id);
  read_sites_.push_back(Read_site{.name       = std::move(name),
                                  .start_byte = ts_node_start_byte(id),
                                  .end_byte   = ts_node_end_byte(id),
                                  .start_line = sp.row + 1,
                                  .start_col  = sp.column + 1,
                                  .end_line   = ep.row + 1,
                                  .end_col    = ep.column + 1,
                                  .scope      = no_frame ? Lnast_nid{} : builder.idx_stmts,
                                  .before     = no_frame ? Lnast_nid{} : lnast->get_last_child(builder.idx_stmts),
                                  .generic    = generic});
}

Prp2lnast::Expr_reads Prp2lnast::expr_reads(TSNode e, bool enter_attribute_reads) const {
  Expr_reads          out;
  std::vector<TSNode> todo{e};
  const auto          push_field = [&](TSNode c, const char* field) {
    if (TSNode f = child_by_field(c, field); !ts_node_is_null(f)) {
      todo.push_back(f);
    }
  };
  while (!todo.empty()) {
    TSNode c = todo.back();
    todo.pop_back();
    if (ts_node_is_null(c)) {
      continue;
    }
    const std::string_view ct(ts_node_type(c));
    if (ct == "identifier") {
      if (const auto txt = trim(get_raw_text(c)); txt != "true" && txt != "false" && txt != "nil") {
        out.names.push_back(c);
      }
      continue;
    }
    if (ct == "lambda") {
      out.lambdas.push_back(c);
      continue;
    }
    if (ct == "attribute_read" && !enter_attribute_reads) {
      continue;
    }
    if (ct == "dot_expression" || ct == "dot_expression_type" || ct == "member_selection" || ct == "attribute_read"
        || (ct == "expression_type" && ts_node_named_child_count(c) > 1)) {
      // Only the base reads a name; the rest are field or attribute names.
      // `std.clog2` names a built-in, not a value.
      if (ts_node_named_child_count(c) == 0) {
        continue;
      }
      TSNode base = ts_node_named_child(c, 0);
      if (ct != "attribute_read" && ts_node_named_child_count(c) > 1 && trim(get_text(base)) == prp_builtins::std_namespace
          && prp_builtins::is_std_member(trim(get_text(ts_node_named_child(c, 1))))) {
        continue;
      }
      todo.push_back(base);
      continue;
    }
    if (ct == "arg_assignment" || ct == "attribute_assignment" || ct == "assignment") {
      push_field(c, "rvalue");  // the lvalue is a key (or a declared name)
      continue;
    }
    if (ct == "typed_identifier" || ct == "typed_field") {
      push_field(c, "type");  // a tuple-TYPE field: its name is not a read
      push_field(c, "definition");
      continue;
    }
    TSNode callee{};
    if (ct == "function_call_expression" || ct == "function_call_type") {
      callee = child_by_field(c, "function");
      if (!ts_node_is_null(callee)) {
        out.callees.push_back(callee);
      }
    }
    for (TSNode k : ts_node_named_children(c)) {
      if (ts_node_is_null(callee) || ts_node_start_byte(k) != ts_node_start_byte(callee)) {
        todo.push_back(k);
      }
    }
  }
  return out;
}

void Prp2lnast::record_name_reads(TSNode e, Generic_read generic) {
  // A streamed lambda's signature has no stmts frame yet: the site resolves
  // against streamed_scope_names_ (see read_is_visible). Anywhere else (a
  // statement, a tuple literal, a kept func_def's signature) the site records
  // at the builder position, as identifier_to_node does; the names an
  // in-flight construct introduces (earlier tuple fields, earlier params and
  // the generics of the signature being lowered) resolve right here.
  const bool no_frame = is_streamed_signature() || builder.idx_stmts.is_invalid();
  for (TSNode c : expr_reads(e, /*enter_attribute_reads=*/true).names) {
    std::string name{canonical_escaped_ident(trim(get_text(c)))};
    if (str_tools::is_pyrope_type_word(name) || name_in_inflight_scope(name)) {
      continue;  // a bare type word is the type, not a read (`` `U8` `` keeps its backticks)
    }
    note_capture_read(name, c);
    push_read_site(c, std::move(name), no_frame, generic);
  }
}

void Prp2lnast::emit_type_expr(const Lnast_nid& parent, TSNode type_node) {
  std::string_view t(ts_node_type(type_node));
  if (t == "function_call_type"
      || (t == "expression_type" && ts_node_named_child_count(type_node) != 0
          && std::string_view(ts_node_type(ts_node_named_child(type_node, 0))) == "function_call_expression")) {
    const auto raw  = trim(get_text(type_node));
    const auto name = trim(raw.substr(0, raw.find('(')));
    if (const auto renamed = str_tools::renamed_type_spelling(name); !renamed.empty() && !lookup_capture(name)) {
      report_error(type_node,
                   "renamed-type",
                   "type",
                   std::format("`{}` was renamed `{}`", name, renamed),
                   std::format("use `{}` for the built-in type", renamed));
    }
    report_error(type_node,
                 "type-from-call-removed",
                 "type",
                 "a function call cannot be used as a type",
                 "use explicit generic parameters, for example `<T>` and a `:T` annotation");
  }
  if (t == "expression_type") {
    check_type_name_spelling(type_node);
  }
  if (t == "uint_type" || t == "sint_type") {
    // Emit the canonical `prim_type_int(max, min)`. The width sugar
    // `uN`/`sN` computes its bounds; the unsized spellings (`uint`,
    // `unsigned`, `int`, `integer`) leave both bounds "nil" (unbounded). "nil"
    // marks an unbounded bound (a non-integer the consumers read as unset).
    // An optional `constraint` tuple (`int(max=…, min=…, bits=…)`) refines
    // the keyword's bounds.
    TSNode      constraint = child_by_field(type_node, "constraint");
    std::string txt(trim(get_text(type_node)));
    // With a constraint the node text includes the `(…)`; keep the keyword.
    if (auto paren = txt.find('('); paren != std::string::npos) {
      txt = std::string(trim(std::string_view(txt).substr(0, paren)));
    }
    std::string max_txt;
    std::string min_txt;
    if (!ts_node_is_null(constraint)) {
      int_type_call_bounds(txt, constraint, max_txt, min_txt);
    } else if (!txt.empty() && std::isdigit(static_cast<unsigned char>(txt.back()))) {
      // First char is u/s; the remainder is the bit width.
      auto w = try_parse_int_type_width(std::string_view{txt}.substr(1));
      if (!w) {
        report_error(type_node,
                     "width-too-large",
                     "type",
                     std::format("integer type width '{}' is out of range (0..{} bits)", txt, kMaxIntTypeWidth),
                     "use a smaller bit width");
      }
      const int  n         = *w;
      const bool is_signed = (t == "sint_type");
      if (is_signed) {
        // s1 is {-1,0}, s2 is {-2..1}: 2^(n-1)-1 down to -2^(n-1).
        max_txt = std::string(Dlop::get_mask_value(n - 1)->to_pyrope());
        min_txt = std::string(Dlop::get_neg_mask_value(n - 1)->to_pyrope());
      } else {
        max_txt = std::string(Dlop::get_mask_value(n)->to_pyrope());
        min_txt = "0";
      }
    } else if (txt == "Unsigned") {
      min_txt = "0";  // docs 04-variables: `Unsigned` is `Signed(min=0)`
    }
    auto                     idx = lnast->add_child(parent, Lnast_ntype::create_prim_type_int());
    // 2f-type_bound — a bound prelower_type_bounds could not fold rides as a
    // REF to the statements it emitted just above; the runner folds it once
    // the generic is bound (bake_decl_pre_step). Everything else stays a const.
    const Prelowered_bounds* pre = nullptr;
    if (!ts_node_is_null(constraint)) {
      if (const auto it = prelowered_int_bounds_.find(ts_node_start_byte(constraint)); it != prelowered_int_bounds_.end()) {
        pre = &it->second;
      }
    }
    if (pre != nullptr && !pre->max.is_invalid()) {
      lnast->add_child(idx, pre->max);
    } else {
      lnast->add_child(idx, Lnast_node::create_const(max_txt.empty() ? "nil" : max_txt));
    }
    if (pre != nullptr && !pre->min.is_invalid()) {
      lnast->add_child(idx, pre->min);
    } else {
      lnast->add_child(idx, Lnast_node::create_const(min_txt.empty() ? "nil" : min_txt));
    }
  } else if (t == "bool_type") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_bool());
  } else if (t == "string_type") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_string());
  } else if (t == "clock_type") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_clock());
  } else if (t == "reset_type") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_reset());
  } else if (t == "array_type") {
    auto   idx  = lnast->add_child(parent, Lnast_ntype::create_comp_type_array());
    TSNode base = child_by_field(type_node, "base");
    if (!ts_node_is_null(base)) {
      emit_type_expr(idx, base);
    }
    TSNode len = child_by_field(type_node, "length");
    if (!ts_node_is_null(len)) {
      lnast->add_child(idx, array_dim_to_node(len));
    }
  } else if (t == "expression_type" || t == "dot_expression_type" || t == "function_call_type") {
    // A bare literal used as a type — `:3333`, `:[4]3333`, `:true`, `:"s"` —
    // is a value, never a type. The parser wraps it as `expression_type`
    // containing a `constant`; a real named type wraps an `identifier` (and may
    // resolve late, via import/generic/forward-ref, so it is NOT validated
    // here). Reject the unambiguous constant form now with a clean error
    // instead of letting the bogus `ref("3333")` reach lnastfmt as an internal
    // "not a valid identifier" malformation (or slip through silently).
    if (t == "expression_type" && ts_node_named_child_count(type_node) >= 1
        && std::string_view(ts_node_type(ts_node_named_child(type_node, 0))) == "constant") {
      report_error(type_node,
                   "not-a-type",
                   "type",
                   std::format("`{}` is a value, not a type", trim(get_text(type_node))),
                   "use a type here, e.g. `U4`/`Bool`/`String` or a declared `type`/`enum` name");
    }
    // A named type is a `ref` to its symbol-table binding (resolved
    // via that name's bundle); the typename is still tracked by the separate
    // `attr_set(typename,…)` emitted in emit_type_spec. Replaces expr_type.
    record_type_name_read(type_node);  // `:potato` / `:[N]potato` must resolve to a real type
    lnast->add_child(parent, Lnast_node::create_ref(trim(get_text(type_node))));
  } else {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_none());
  }
}

void Prp2lnast::emit_bare_type_word(const Lnast_nid& parent, std::string_view word, TSNode anchor) {
  if (word == "Bool") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_bool());
    return;
  }
  if (word == "String") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_string());
    return;
  }
  if (word == "Clock") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_clock());
    return;
  }
  if (word == "Reset") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_reset());
    return;
  }
  // `U<N>` / `S<N>` / `Unsigned` / `Signed` (the emit_type_expr rule without a
  // constraint: the unsized words leave both bounds nil).
  std::string max_txt;
  std::string min_txt;
  if (word.size() >= 2 && std::isdigit(static_cast<unsigned char>(word[1]))) {
    const auto w = try_parse_int_type_width(word.substr(1));
    if (!w) {
      report_error(anchor,
                   "width-too-large",
                   "type",
                   std::format("integer type width '{}' is out of range (0..{} bits)", word, kMaxIntTypeWidth),
                   "use a smaller bit width");
    }
    const int n = *w;
    if (word[0] == 'S') {
      max_txt = std::string(Dlop::get_mask_value(n - 1)->to_pyrope());
      min_txt = std::string(Dlop::get_neg_mask_value(n - 1)->to_pyrope());
    } else {
      max_txt = std::string(Dlop::get_mask_value(n)->to_pyrope());
      min_txt = "0";
    }
  } else if (word == "Unsigned") {
    min_txt = "0";  // docs 04-variables: `Unsigned` is `Signed(min=0)`
  }
  auto idx = lnast->add_child(parent, Lnast_ntype::create_prim_type_int());
  lnast->add_child(idx, Lnast_node::create_const(max_txt.empty() ? "nil" : max_txt));
  lnast->add_child(idx, Lnast_node::create_const(min_txt.empty() ? "nil" : min_txt));
}

Lnast_node Prp2lnast::type_word_value(TSNode n, std::string_view word) {
  auto didx = builder.add_child(Lnast_ntype::create_declare());
  auto tref = builder.mint_tmp_ref();
  lnast->add_child(didx, tref);
  emit_bare_type_word(didx, word, n);
  lnast->add_child(didx, Lnast_node::create_const("type"));
  return tref;
}

void Prp2lnast::reject_common_mistakes_attr_name(TSNode node, std::string_view name, bool has_value) const {
  // `lg` is the explicit lgraph/module-name attribute. It is ONLY valid on a
  // `pub` lambda name (`pub comb f::[lg="name"]`), where it is consumed by
  // process_lambda_statement_named before reaching this path. Any `lg` that
  // arrives here is attached to a non-lambda (a variable/expression attribute
  // bracket), which has no module artifact to rename — reject it. (2f-lg)
  if (name == "lg") {
    report_error(node,
                 "lg-not-lambda",
                 "type",
                 "the `lg` attribute is only valid on a `pub` lambda definition, not on a value",
                 "move `lg=\"name\"` onto the `pub comb`/`mod`/`pipe` definition");
    return;
  }

  // Unknown attribute names are normally pass-through (user extensibility —
  // attributes_spec.md §1.D), so this only errors on a curated list of
  // popular mistakes plus singular/plural near-misses of the built-in
  // vocabulary. Anything else is silently accepted as a user attribute.

  // Pyrope-source attribute vocabulary — THE SINGLE LIST, owned by
  // upass/core/battr.hpp and reached through the header-only
  // //upass/core:battr_hdr target (inou/prp still does not LINK the upass/core
  // plugin; it only includes this predicate).
  //
  // This used to be a hand-copied array "kept in sync" by comment, and it was
  // not: it accepted `ubits`/`sbits` after those reads were removed, it lacked
  // the live `inp`/`out`/`id`/`fields` introspection reads, and battr carried a
  // `defer` the language had dropped. Do not reintroduce a local copy — add the
  // name to battr.hpp and both sides learn it at once.
  auto is_known = [](std::string_view n) { return battr::is_builtin_attr_name(n); };

  // `clock`/`reset` signal-classification attributes (`clk::[clock]`) are
  // gone: a clock or reset is declared with the `Clock`/`Reset` TYPE (docs 04b
  // "Synthesis attribute list"), and a register picks one with `*_pin`.
  if (name == "clock" || name == "reset") {
    report_error(node,
                 "attr-name-mistake",
                 "syntax",
                 std::format("there is no `{}` attribute: a {} is declared by its type (`:{}`), not by an attribute",
                             name,
                             name,
                             name == "clock" ? "Clock" : "Reset"),
                 std::format("declare the input `:{}`; to pick a register's {} use `{}_pin=<wire>`",
                             name == "clock" ? "Clock" : "Reset",
                             name,
                             name));
  }
  if (is_known(name)) {
    // The INVERSE rule, for the pin family: each of these NAMES A SIGNAL, so a
    // flag-only spelling says nothing. Without this the value defaults to the
    // text `true` and the mistake only surfaces much later, as tolg's
    // "reg 'r' names enable 'true' but 'top' has no such input/wire" — a
    // message about a signal the user never wrote.
    if (!has_value && (name == "enable" || name == "clock_pin" || name == "reset_pin")) {
      report_error(
          node,
          "attr-needs-value",
          "syntax",
          std::format("attribute `{}` needs a value — it names the signal it binds, it is not a flag", name),
          name == "enable" ? "write `enable=<condition>` (e.g. `enable=(wen != 0)`)" : std::format("write `{}=<wire>`", name));
    }
    return;
  }

  // Popular renames — habits from Verilog/other HDLs or the LGraph pin names.
  struct Mistake {
    std::string_view wrong;
    std::string_view hint;
  };
  static constexpr Mistake mistakes[] = {
      // `initial` is the canonical spelling in BOTH layers now: it is the
      // LGraph Flop/Latch/Memory reset-value pin (graph/cell.cpp) and the
      // Pyrope declaration attribute. `init` was the old attribute spelling.
      {    "init",                                                     "use `initial`"                  },
      // Deleted from the vocabulary — without a row here each of these would
      // silently fold to nil as a user attribute instead of erroring.
      {  "inputs",                           "use `inp` to read a lambda's input port names (`f.[inp]`)"},
      { "outputs",                          "use `out` to read a lambda's output port names (`f.[out]`)"},
      {   "ubits",        "there is no `ubits` attribute: use `bits`, and read the sign with `x.[sign]`"},
      {   "sbits",        "there is no `sbits` attribute: use `bits`, and read the sign with `x.[sign]`"},
      {   "defer", "the `.[defer]` end-of-cycle read was removed — forward-declare a `wire` and read it"},
      // `.[loc]`/`.[file]` were registered but nothing ever derived them (the
      // documented `puts` example printed nil for both); removed from the
      // vocabulary, so pin the error here.
      {     "loc",                         "the `.[loc]` / `.[file]` source-location reads were removed"},
      {    "file",                         "the `.[loc]` / `.[file]` source-location reads were removed"},
      // `saturate` was an alias only the CALL form honoured (`saturate x = …`
      // never parsed); `sat` is the one spelling, as in prp_keywords.def.
      {"saturate",                                                                           "use `sat`"},
      {     "clk", "use `clock_pin=<wire>` to pick a register's clock (a clock input is typed `:Clock`)"},
      {     "rst", "use `reset_pin=<wire>` to pick a register's reset (a reset input is typed `:Reset`)"},
      {   "width",  "use `bits` to read a width (`x.[bits]`); to set a width declare a type, e.g. `:U8`"},
      {      "en",                                                                        "use `enable`"},
      { "posedge",                                                        "use `posclk` (`posclk=true`)"},
      { "negedge",                                                                  "use `posclk=false`"},
      {  "signed",     "signedness is derived from the type — declare `:S<N>` (read it with `x.[sign]`)"},
      {"unsigned",     "signedness is derived from the type — declare `:U<N>` (read it with `x.[sign]`)"},
      // A proposed-but-never-adopted spelling: whether an array is a flop bank
      // or a memory is the synthesis flow's call, and a `reg` array with a
      // reset value already resets every entry in one cycle (08-memories.md).
      { "storage",
       "there is no `storage` attribute: a `reg` array with a reset value already resets every entry in one cycle (see "
       "08-memories.md); an array is registers or a memory by the synthesis flow, not by attribute"     },
  };
  for (const auto& m : mistakes) {
    if (name == m.wrong) {
      report_error(node, "attr-name-mistake", "syntax", std::format("`{}` is not a Pyrope attribute name", name), m.hint);
    }
  }

  // Singular/plural near-miss of a built-in name (`bit`→`bits`, `keys`→`key`).
  if (is_known(std::string(name) + "s")) {
    report_error(node,
                 "attr-name-mistake",
                 "syntax",
                 std::format("`{}` is not a Pyrope attribute name", name),
                 std::format("did you mean `{}s`?", name));
  }
  if (name.size() > 1 && name.back() == 's' && is_known(name.substr(0, name.size() - 1))) {
    report_error(node,
                 "attr-name-mistake",
                 "syntax",
                 std::format("`{}` is not a Pyrope attribute name", name),
                 std::format("did you mean `{}`?", name.substr(0, name.size() - 1)));
  }
}

Lnast_node Prp2lnast::io_default_to_node(TSNode def, TSNode param) {
  if (!builder.idx_stmts.is_invalid()) {
    return expr_to_node(def);  // a func_def kept in its enclosing tree: lowered there
  }
  return comptime_default_node(def, param);
}

bool Prp2lnast::input_default_in_prologue(TSNode def, std::string_view lambda_kind, bool is_io_output, bool is_vararg) {
  if (ts_node_is_null(def) || is_io_output || is_vararg) {
    return false;
  }
  if (lambda_kind == "comb") {
    return true;
  }
  if (lambda_kind != "mod" && lambda_kind != "pipe") {
    return false;
  }
  return !front_end_default_value(def).has_value();  // else the io slot holds the constant
}

std::optional<Lnast_node> Prp2lnast::front_end_default_value(TSNode def) {
  TSNode lit = def;
  while ((std::string_view(ts_node_type(lit)) == "expression_type" || std::string_view(ts_node_type(lit)) == "paren_group")
         && ts_node_named_child_count(lit) == 1) {
    lit = ts_node_named_child(lit, 0);
  }
  // A literal first: expr_to_node reports a malformed one (`0b1010` needs a
  // sign) at its span, and emits no statement.
  const std::string_view lt(ts_node_type(lit));
  if (const auto txt = trim(get_text(lit));
      lt == "constant" || (lt == "identifier" && (txt == "true" || txt == "false" || txt == "nil"))) {
    return expr_to_node(lit);
  }
  if (const auto v = resolve_type_int_value(def); v && v->is_integer()) {
    return Lnast_node::create_const(std::string(v->to_pyrope()));
  }
  // An enum entry (`c:Color = Color.Green`): its integer encoding.
  if ((lt == "dot_expression" || lt == "expression_type") && ts_node_named_child_count(lit) == 2
      && std::string_view(ts_node_type(ts_node_named_child(lit, 0))) == "identifier"
      && std::string_view(ts_node_type(ts_node_named_child(lit, 1))) == "identifier") {
    if (const auto v = enum_entry_value(trim(get_text(ts_node_named_child(lit, 0))), trim(get_text(ts_node_named_child(lit, 1))))) {
      return Lnast_node::create_const(std::string(v->to_pyrope()));
    }
  }
  return std::nullopt;
}

namespace {
constexpr std::string_view kDefaultNotComptimeHint
    = "a `mod`/`pipe` input default (and an output register's reset value) must fold to a compile-time constant: a "
      "literal, a generic, a `comptime const`, an attribute read, an enum entry, or a comb call or expression over "
      "them; or pass the argument at every call";
}  // namespace

void Prp2lnast::check_default_reads_no_input(TSNode def, TSNode param) {
  for (TSNode c : expr_reads(def, /*enter_attribute_reads=*/false).names) {
    const std::string name{canonical_escaped_ident(trim(get_text(c)))};
    if (const auto hit = lookup_capture(name); hit && hit->kind == Bind_kind::runtime && name_in_inflight_scope(name)) {
      report_error(def,
                   "default-not-comptime",
                   "type",
                   std::format("the default of `{}` reads the runtime input `{}`", trim(get_text(param)), name),
                   kDefaultNotComptimeHint);
    }
  }
}

Lnast_node Prp2lnast::comptime_default_node(TSNode def, TSNode param) {
  if (auto v = front_end_default_value(def)) {
    return *v;
  }
  // Anything else needs statements, which this position has none to compute
  // in. A name nothing declares is an undefined-read (never a nil default):
  // check it here, since the default-not-comptime error below ends the parse
  // before check_undefined_reads runs. Conservative: any binding in scope, an
  // in-flight or streamed-scope name, a lambda, a type or a built-in counts as
  // declared.
  absl::flat_hash_set<std::string> hoisted;
  collect_hoisted_names(*lnast, lnast->get_root(), hoisted);
  for (const auto& st : destination_stack_) {
    collect_hoisted_names(*st.lnast, st.lnast->get_root(), hoisted);
  }
  for (TSNode c : expr_reads(def, /*enter_attribute_reads=*/true).names) {
    const std::string name{canonical_escaped_ident(trim(get_text(c)))};
    note_capture_read(name, c);
    if (!lookup_capture(name) && !name_in_inflight_scope(name) && !streamed_scope_names_.contains(name)
        && !streamed_function_names_.contains(name) && !hoisted.contains(name) && !str_tools::is_pyrope_type_word(name)
        && name != "self" && name != prp_builtins::std_namespace) {
      report_error(c, "undefined-read", "name", prp_undefined_read_message(name), prp_undefined_read_hint);
    }
  }
  check_default_reads_no_input(def, param);
  report_error(def,
               "default-not-comptime",
               "type",
               std::format("the default of `{}` must be a compile-time constant", trim(get_text(param))),
               kDefaultNotComptimeHint);
}

void Prp2lnast::emit_arg_assign(const Lnast_nid& tuple_parent, TSNode typed_ident, TSNode definition_or_null, bool is_ref_mod,
                                std::vector<Param_attr>* attrs_out, std::string_view lambda_kind, bool is_io_output,
                                bool is_vararg_mod) {
  TSNode id = child_by_field(typed_ident, "identifier");
  if (ts_node_is_null(id)) {
    return;
  }
  auto aidx = lnast->add_child(tuple_parent, Lnast_ntype::create_store());
  // Canonicalize a `` `name` `` param/output exactly as a read does, so the
  // declaration and its body reads resolve to the same symbol (round-trip).
  lnast->add_child(aidx, Lnast_node::create_ref(canonical_escaped_ident(get_text(id))));
  // Default-value slot. Encoding choice for the absent case:
  //   `...` mod          -> const "..."  (var-args marker)
  //   `ref` mod          -> const "ref"  (write-back marker for the inliner)
  //   no default, no mod -> const "nil"
  //   explicit default   -> expr_to_node(definition)
  // expr_to_node may emit tmp statements; for literal defaults (the common
  // case) it returns a const node and has no side effect. `...`/`ref`/`reg`
  // are mutually exclusive in the grammar's `mod` field, so the markers never
  // collide. A var-arg never carries a default — the `...` marker WINS over any
  // (meaningless) `= expr` so downstream var-arg detection (which keys on the
  // sentinel) is never silently lost if the grammar admits `...args = e`.
  // A comb INPUT default is lowered later as a body-prologue store into its
  // default-value local (evaluated in param-tuple scope, self-contained →
  // survives extraction, todo 3g E): the io slot only records that a default
  // EXISTS via the `__default` sentinel (SSA turns it into
  // io_meta.has_default). Emitting it here via expr_to_node would (for an
  // expression default) pollute the ENCLOSING frame with a dangling tmp — so
  // the sentinel replaces it. So does a mod/pipe default that does not fold
  // here (input_default_in_prologue). Outputs and the other mod / pipe
  // defaults keep the direct encoding (a compile-time constant).
  if (!ts_node_is_null(definition_or_null) && !is_io_output && (lambda_kind == "mod" || lambda_kind == "pipe")) {
    check_default_reads_no_input(definition_or_null, id);
  }
  const bool defer_default = input_default_in_prologue(definition_or_null, lambda_kind, is_io_output, is_vararg_mod);
  Lnast_node arg_val       = is_vararg_mod                          ? Lnast_node::create_const("...")
                             : defer_default                        ? Lnast_node::create_const("__default")
                             : !ts_node_is_null(definition_or_null) ? io_default_to_node(definition_or_null, id)
                             : is_ref_mod                           ? Lnast_node::create_const("ref")
                                                                    : Lnast_node::create_const("nil");
  lnast->add_child(aidx, arg_val);
  // Optional type subtree (3rd child).
  TSNode type_cast = child_by_field(typed_ident, "type");
  if (!ts_node_is_null(type_cast)) {
    TSNode ty = child_by_field(type_cast, "type");
    if (!ts_node_is_null(ty) && lambda_kind == "comb" && !is_io_output) {
      // A comb holds no state (docs 04b "Implicit clock and reset"): a `Clock`
      // or `Reset` input is a compile error, and nothing is auto-wired into it.
      if (const std::string_view tk(ts_node_type(ty)); tk == "clock_type" || tk == "reset_type") {
        const bool clk = tk == "clock_type";
        report_error(type_cast,
                     "comb-clock-reset-input",
                     "type",
                     std::format("`comb` input `{}` is a `{}`: a comb holds no state and can not declare a {} input",
                                 trim(get_text(id)),
                                 clk ? "Clock" : "Reset",
                                 clk ? "Clock" : "Reset"),
                     clk ? "make it a `mod` (a clocked module), or pass the value as data (`:Bool`)"
                         : "make it a `mod`, or pass the value as data (`:Bool`)");
      }
    }
    if (!ts_node_is_null(ty)) {
      // A literal default must match the declared scalar kind, exactly as a
      // variable declaration `mut b:bool = 3` would (shared check). When the
      // default was deferred to the body prologue (arg_val is the sentinel, not
      // the literal), rebuild a lightweight const from the default's source text
      // for the same check: a LITERAL (`3`, `true`) keeps its kind so
      // `b:bool = 3` still errors; an EXPRESSION default (`a+5`) yields a const
      // of unknown kind → the check no-ops (it is kind-checked when the prologue
      // store lowers).
      if (defer_default) {
        auto lit = Lnast_node::create_const(std::string(trim(get_text(definition_or_null))));
        check_decl_init_kind(trim(get_text(id)), lit, ty, type_cast);
      } else {
        check_decl_init_kind(trim(get_text(id)), arg_val, ty, type_cast);
      }
      // 2f-generic_port_width — see Pending_port_bound. Also a field of a
      // tuple-typed port (lambda_kind is empty there).
      defer_port_bound(type_cast, aidx);
      emit_arg_type(aidx, ty);
    }
    // Parameter-side attribute carrier (`a::[comptime]`, `a:T::[debug=2]`).
    // attribute_sq hangs off the type_cast with field "attribute"; collect
    // (key, value) pairs into the out-vector. The lambda-def driver replays
    // them as `attr_set` (+ a `cassert` for `comptime`) at body entry.
    if (attrs_out) {
      uint32_t total = ts_node_child_count(type_cast);
      for (uint32_t i = 0; i < total; i++) {
        TSNode           c     = ts_node_child(type_cast, i);
        const char*      fname = ts_node_field_name_for_child(type_cast, i);
        std::string_view ct(ts_node_type(c));
        if (ct != "attribute_sq" && ct != "tuple_sq") {
          continue;
        }
        if (!fname || std::string_view(fname) != "attribute") {
          continue;
        }
        for (TSNode item : ts_node_named_children(c)) {
          std::string_view it(ts_node_type(item));
          if (it == "assignment" || it == "attribute_assignment") {
            TSNode lv = child_by_field(item, "lvalue");
            TSNode rv = child_by_field(item, "rvalue");
            if (ts_node_is_null(lv)) {
              continue;
            }
            std::string_view key_txt;
            std::string_view lvt(ts_node_type(lv));
            if (lvt == "typed_identifier") {
              TSNode iid = child_by_field(lv, "identifier");
              if (!ts_node_is_null(iid)) {
                key_txt = trim(get_text(iid));
              }
            } else {
              key_txt = trim(get_text(lv));
            }
            if (key_txt.empty()) {
              continue;
            }
            std::string val_txt = ts_node_is_null(rv) ? std::string{} : std::string(trim(get_text(rv)));
            if (key_txt == "synth" || key_txt.starts_with("synth.")) {
              continue;
            }
            reject_common_mistakes_attr_name(lv, key_txt, !ts_node_is_null(rv));
            attrs_out->push_back({std::string(get_text(id)), std::string(key_txt), std::move(val_txt)});
          } else if (it == "identifier" || it == "ref_identifier") {
            auto kt = trim(get_text(item));
            if (kt.empty()) {
              continue;
            }
            reject_common_mistakes_attr_name(item, kt, false);
            attrs_out->push_back({std::string(get_text(id)), std::string(kt), std::string{}});
          }
        }
      }
    }
  }

  // Interface timing contract. Only enforced when called from the
  // lambda io driver (lambda_kind set); tuple-literal contexts skip it.
  if (!lambda_kind.empty()) {
    auto   arg_name = trim(get_text(id));
    // `@[...]` rides the type_cast's `timing` field when a type is present
    // (`x:u8@[2]`), or the typed_identifier's own `timing` field when not
    // (`x@[2]`).
    TSNode timing   = child_by_field(typed_ident, "timing");
    if (ts_node_is_null(timing) && !ts_node_is_null(type_cast)) {
      timing = child_by_field(type_cast, "timing");
    }
    const bool is_mod  = lambda_kind == "mod";
    const bool is_pipe = lambda_kind == "pipe";

    if (!ts_node_is_null(timing) && !is_io_output) {
      report_error(timing,
                   "io-input-timing",
                   "type",
                   std::format("input '{}' declares a landing cycle: inputs are at cycle 0 by definition", arg_name),
                   "drop the `@[...]` from the input");
    }
    if (!ts_node_is_null(timing) && is_io_output && !is_mod) {
      report_error(
          timing,
          "io-output-timing",
          "type",
          std::format("{} output '{}' declares a landing cycle: per-output cycles are a `mod` feature", lambda_kind, arg_name),
          is_pipe ? "a pipe's uniform latency is declared on the keyword (`pipe[N]`); drop the `@[...]`"
                  : "drop the `@[...]` (these outputs land at cycle 0 by definition)");
    }
    // An untyped non-`self` `pipe`/`mod` input/output is no longer a
    // definition-time error: the lambda becomes a deferred TEMPLATE (no LGraph
    // until a call site supplies the concrete types). func_extract stamps the
    // template flag; the specializer mints a concrete module per call signature
    // (an untyped *actual* into such a port is the call-site error instead).
    // The deleted check was `(is_mod || is_pipe) && !has_type && arg_name !=
    // "self"` → "lambda-needs-types".
    if (is_mod && is_io_output) {
      if (ts_node_is_null(timing)) {
        report_error(
            id,
            "mod-output-needs-cycle",
            "type",
            std::format("mod output '{}' declares no landing cycle: every mod output carries its cycle at the interface", arg_name),
            "declare it as `name:T@[N]` (N=0 is a combinational feedthrough), or opt out with `name:T@[]`");
      }
      // Stamp stages(min,max) as the TRAILING child of this io store (after
      // the optional type subtree; downstream identifies it by ntype, never
      // by position — the same convention as the pipe stamping in
      // process_lambda_statement_named). `@[]` keeps the slot present with
      // min/max = nil (unconstrained — the form foreign Verilog modules,
      // which carry no markings, ingest as).
      std::string min_txt = "nil";
      std::string max_txt = "nil";
      TSNode      range_n = child_by_field(timing, "range");
      if (!ts_node_is_null(range_n)) {
        report_error(range_n,
                     "invalid-mod-cycle",
                     "type",
                     std::format("mod output '{}' declares an open-ended cycle range", arg_name),
                     "write `@[N]`, a closed range `@[A..=B]` / `@[A..<B]`, or opt out with `@[]`");
      }
      constexpr int64_t k_max_cycle = std::numeric_limits<int32_t>::max();
      TSNode            index_n     = child_by_field(timing, "index");
      bool              got_range   = false;
      // An output reachable through paths of different depths (e.g. mux arms
      // at different cycles) declares the interval with `@[A..=B]`/`@[A..<B]`
      // (same expression_item shape as a stage[A..=B] slot).
      if (!ts_node_is_null(index_n) && std::string_view(ts_node_type(index_n)) == "expression_item"
          && ts_node_named_child_count(index_n) == 3) {
        TSNode lhs = ts_node_named_child(index_n, 0);
        TSNode op  = ts_node_named_child(index_n, 1);
        TSNode rhs = ts_node_named_child(index_n, 2);
        if (std::string_view(ts_node_type(op)) == "binary_other_op") {
          TSNode op_inner = ts_node_named_child(op, 0);
          if (!ts_node_is_null(op_inner)) {
            std::string_view op_kind(ts_node_type(op_inner));
            bool             inclusive = op_kind == "op_range_inclusive";
            if (inclusive || op_kind == "op_range_exclusive") {
              auto lo = resolve_cycle_value(lhs);
              auto hi = resolve_cycle_value(rhs);
              if (lo && hi) {
                int64_t cmax = inclusive ? *hi : *hi - 1;
                if (*lo < 0 || cmax < *lo) {
                  report_error(op,
                               "invalid-mod-cycle",
                               "type",
                               std::format("invalid landing-cycle range [{}, {}] for mod output '{}': cycles start at 0 "
                                           "and only ascending ranges are allowed",
                                           *lo,
                                           cmax,
                                           arg_name),
                               "write an ascending range like `@[1..=2]`");
                }
                if (cmax > k_max_cycle) {
                  report_error(rhs,
                               "invalid-mod-cycle",
                               "type",
                               std::format("invalid landing cycle {} for mod output '{}': exceeds the maximum of {}",
                                           cmax,
                                           arg_name,
                                           k_max_cycle),
                               "use a realistic cycle count");
                }
                min_txt   = std::to_string(*lo);
                max_txt   = std::to_string(cmax);
                got_range = true;
              }
            }
          }
        }
      }
      if (!ts_node_is_null(index_n) && !got_range) {
        std::optional<int64_t> lit = resolve_cycle_value(index_n);
        if (const auto cpt = lit ? std::nullopt : unfolded_comptime_read(index_n)) {
          report_error(index_n,
                       "invalid-mod-cycle",
                       "type",
                       std::format("the landing cycle of mod output '{}' reads comptime const `{}`, whose value is computed by "
                                   "statements (a loop, an `if`, a call) this declaration cannot evaluate",
                                   arg_name,
                                   *cpt),
                       "use a literal, or a comptime const folded from literals (`comptime const D = N + 1`)");
        }
        if (!lit) {
          report_error(index_n,
                       "invalid-mod-cycle",
                       "type",
                       std::format("mod output '{}' must declare its landing cycle (a literal, compile-time constant, or "
                                   "closed ascending range)",
                                   arg_name),
                       "write `@[N]` / `@[A..=B]` with literals (or a `const` resolvable to one), or opt out with `@[]`");
        }
        if (*lit < 0) {
          report_error(index_n,
                       "invalid-mod-cycle",
                       "type",
                       std::format("invalid landing cycle {} for mod output '{}': cycles count from the inputs and start at 0",
                                   *lit,
                                   arg_name),
                       "use `@[0]` for a combinational feedthrough output");
        }
        if (*lit > k_max_cycle) {
          report_error(
              index_n,
              "invalid-mod-cycle",
              "type",
              std::format("invalid landing cycle {} for mod output '{}': exceeds the maximum of {}", *lit, arg_name, k_max_cycle),
              "use a realistic cycle count");
        }
        min_txt = std::to_string(*lit);
        max_txt = min_txt;
      }
      auto st = lnast->add_child(aidx, Lnast_ntype::create_stages());
      lnast->add_child(st, Lnast_node::create_const(min_txt));
      lnast->add_child(st, Lnast_node::create_const(max_txt));
    }
  }
}

void Prp2lnast::emit_arg_type(const Lnast_nid& assign_parent, TSNode type_node) {
  // Composite tuple type `(name:T, name2:U, ...)` lowers to a `tuple_add` whose
  // children mirror the inputs/outputs shape: each field is a recursive
  // `assign(ref, default-or-nil, type?)`. A bare `name:T` field parses as a
  // `typed_field`; `name:T = default` is an `assignment` whose lvalue is a
  // `typed_identifier`.
  std::string_view t(ts_node_type(type_node));
  if (t == "expression_type") {
    for (TSNode inner : ts_node_named_children(type_node)) {
      if (std::string_view(ts_node_type(inner)) != "tuple") {
        continue;
      }
      auto   tup_idx      = lnast->add_child(assign_parent, Lnast_ntype::create_tuple_add());
      size_t anon_entries = 0;  // `_:T` entries seen: the position of the next one
      bool   named_entry  = false;
      for (TSNode item : ts_node_named_children(inner)) {
        std::string_view it(ts_node_type(item));
        if (it == "identifier") {
          // A bare type entry (`v:(U4, U8)`) has no name and no `_:` marker.
          report_error(item,
                       "unnamed-tuple-type-field",
                       "type",
                       std::format("a tuple-type entry must be named (`name:T`) or anonymous (`_:T`): a bare `{}` is neither",
                                   trim(get_text(item))),
                       "write `_:T` for a positional entry (`v:(_:U4, _:U8)`), or `v:[]` for any tuple or array");
          continue;
        }
        if (it == "typed_identifier") {
          named_entry = true;
          emit_arg_assign(tup_idx, item, TSNode{}, /*is_ref_mod=*/false);
        } else if (it == "typed_field") {
          // Bare `name:type` field (identifier: name, type: type_cast). Emit
          // an arg-shape assign directly: ref(name), const "nil", type-subtree.
          TSNode arg = child_by_field(item, "identifier");
          TSNode tc  = child_by_field(item, "type");
          TSNode ty  = ts_node_is_null(tc) ? TSNode{} : child_by_field(tc, "type");
          if (ts_node_is_null(arg)) {
            continue;
          }
          // `_:T` is the anonymous POSITIONAL entry: it is carried as the marker
          // `__pos<N>` (upass.ssa flattens it to the leaf `port.<N>`, read as `v[N]`).
          const bool anon = trim(get_text(arg)) == "_";
          if (anon) {
            if (named_entry) {
              report_error(item,
                           "mixed-tuple-type-fields",
                           "type",
                           "a tuple type is all-named or all-unnamed: `_:T` cannot be mixed with named entries",
                           "name every entry (`a:T, b:U`) or none (`_:T, _:U`)");
            }
          } else {
            named_entry = true;
            if (anon_entries != 0) {
              report_error(item,
                           "mixed-tuple-type-fields",
                           "type",
                           "a tuple type is all-named or all-unnamed: a named entry cannot follow `_:T`",
                           "name every entry (`a:T, b:U`) or none (`_:T, _:U`)");
            }
          }
          auto aidx = lnast->add_child(tup_idx, Lnast_ntype::create_store());
          lnast->add_child(aidx,
                           Lnast_node::create_ref(anon ? std::format("__pos{}", anon_entries++)
                                                       : std::string(canonical_escaped_ident(trim(get_text(arg))))));
          lnast->add_child(aidx, Lnast_node::create_const("nil"));
          if (!ts_node_is_null(ty)) {
            defer_port_bound(tc, aidx);
            emit_arg_type(aidx, ty);
          }
        } else if (it == "assignment") {
          // `name:T = default` field. Lvalue can be typed_identifier or a
          // bare identifier with optional type. Rvalue is the default.
          TSNode lv = child_by_field(item, "lvalue");
          TSNode rv = child_by_field(item, "rvalue");
          if (ts_node_is_null(lv)) {
            continue;
          }
          named_entry = true;
          if (std::string_view(ts_node_type(lv)) == "typed_identifier") {
            emit_arg_assign(tup_idx, lv, rv, /*is_ref_mod=*/false);
          }
        }
      }
      return;
    }
  }
  emit_type_expr(assign_parent, type_node);
}

// Ruling 2026-09-27 #8 (04b-attributes.md): `async` is the canonical
// reset-kind attribute -- `sync=` is still accepted, with a deprecation
// warning. A `*_pin` attribute CONNECTS a wire (docs 04b "Attributes"): it
// takes the signal directly (`clock_pin=clk2`, `reset_pin=any_rst`); the older
// `ref` spelling is an error (ruling 82: the Clock/Reset types make it
// unnecessary), and so is an expression (`reset_pin=rst or soft_rst`, user
// ruling 2026-10-01: name a computed reset first). A `Clock` is never bound to a constant: a constant
// `clock_pin` is reported by upass.tolg (`clock-const`), where every front
// end's constant clock lands.
void Prp2lnast::check_attribute_value(TSNode item, std::string_view key, TSNode rv) const {
  // A `ref` anywhere in the value (`(ref clk)`, a per-port `(ref c0, c1)`).
  const std::function<TSNode(TSNode)> find_ref = [&](TSNode node) -> TSNode {
    if (std::string_view(ts_node_type(node)) == "ref_identifier") {
      return node;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); ++i) {
      if (const auto r = find_ref(ts_node_named_child(node, i)); !ts_node_is_null(r)) {
        return r;
      }
    }
    return TSNode{};
  };
  if ((key == "clock_pin" || key == "reset_pin") && !ts_node_is_null(rv)) {
    if (const auto r = find_ref(rv); !ts_node_is_null(r)) {
      report_error(r,
                   "pin-attr-ref",
                   "syntax",
                   std::format("`{}` takes the signal directly: `ref` is not allowed here", key),
                   std::format("drop the `ref`: write `{}={}`", key, trim(get_text(ts_node_named_child(r, 0)))));
    } else {
      // User ruling 2026-10-01: a pin NAMES a signal (a `Clock`/`Reset`, a
      // `U1`/`Bool` net, a field of one, or a constant such as `false` = no
      // reset; a per-port memory pin is a tuple of those), never an expression
      // (`reset_pin=rst != 0`). A computed reset gets a name first.
      const std::function<TSNode(TSNode)> find_expr = [&](TSNode node) -> TSNode {
        const std::string_view t(ts_node_type(node));
        if (t == "identifier" || t == "constant" || t == "integer_literal" || t == "bool_literal") {
          return TSNode{};
        }
        if (t == "dot_expression" || t == "paren_group" || t == "tuple") {
          for (uint32_t i = 0; i < ts_node_named_child_count(node); ++i) {
            if (const auto e = find_expr(ts_node_named_child(node, i)); !ts_node_is_null(e)) {
              return e;
            }
          }
          return TSNode{};
        }
        return node;
      };
      if (const auto e = find_expr(rv); !ts_node_is_null(e)) {
        const bool clock = key == "clock_pin";
        report_error(rv,
                     "pin-attr-expression",
                     "syntax",
                     std::format("`{}` must name a {} signal, not an expression: `{}`",
                                 key,
                                 clock ? "`Clock`" : "`Reset`, `U1` or `Bool`",
                                 trim(get_text(rv))),
                     clock ? std::string("name a `Clock` input, a child's `Clock` output, or a gated clock "
                                         "(`const gclk = Clock(clock_pin=clk, enable=en)`, then `clock_pin=gclk`)")
                           : std::format("give the reset a name first (`const soft_rst = {}`) and write "
                                         "`reset_pin=soft_rst`; for an active-low reset name the input and set "
                                         "`negreset=true`",
                                         trim(get_text(rv))));
      }
    }
  }
  if (key == "sync") {
    report_warning(item,
                   "reg-attr-sync-deprecated",
                   "type",
                   "reg attribute `sync` is deprecated; write `async`",
                   "`sync=false` is `async=true`; `sync=true` is the default `async=false`");
  }
}

std::vector<std::pair<std::string, TSNode>> Prp2lnast::synth_attribute_items(TSNode attrs) {
  std::vector<std::pair<std::string, TSNode>> out;
  auto                                        add = [&](std::string key, TSNode value) {
    if (std::any_of(out.begin(), out.end(), [&](const auto& x) { return x.first == key; })) {
      report_error(value, "synth-duplicate", "syntax", "duplicate synthesis attribute synth." + key, "specify each key once");
      return;
    }
    try {
      livehd::synth_attr::validate(key, livehd::synth_attr::literal(trim(get_text(value))));
    } catch (const std::exception& e) {
      report_error(value, "synth-value", "syntax", e.what(), "see syntha.md for the synthesis vocabulary");
      return;
    }
    out.emplace_back(std::move(key), value);
  };
  for (TSNode item : ts_node_named_children(attrs)) {
    TSNode lv = child_by_field(item, "lvalue"), rv = child_by_field(item, "rvalue");
    if (ts_node_is_null(lv)) {
      continue;
    }
    auto key = trim(get_text(lv));
    if (key.starts_with("synth.")) {
      add(std::string(key.substr(6)), rv);
    } else if (key == "synth") {
      if (ts_node_is_null(rv) || trim(get_text(rv)).front() != '(') {
        report_error(item, "synth-tuple", "syntax", "synth= requires a named tuple", "synth=(color=\"crit\", adder=\"cla\")");
        continue;
      }
      for (TSNode field : ts_node_named_children(rv)) {
        TSNode fl = child_by_field(field, "lvalue"), fr = child_by_field(field, "rvalue");
        if (ts_node_is_null(fl)) {
          fl = child_by_field(field, "identifier");
          fr = child_by_field(field, "definition");
        }
        if (ts_node_is_null(fl) || ts_node_is_null(fr)) {
          report_error(field, "synth-tuple", "syntax", "synth tuple fields require key=value", "");
          continue;
        }
        add(std::string(trim(get_text(fl))), fr);
      }
    }
  }
  return out;
}

void Prp2lnast::emit_synth_scope(TSNode attrs, int rank) {
  livehd::synth_attr::Policy p;
  for (const auto& [key, value] : synth_attribute_items(attrs)) {
    p[key] = {livehd::synth_attr::literal(trim(get_text(value))), rank};
  }
  if (p.empty()) {
    return;
  }
  auto idx = builder.add_child(Lnast_ntype::create_attr_set());
  lnast->add_child(idx, Lnast_node::create_ref(std::format("%__synth_scope_{}", region_marker_seq_++)));
  lnast->add_child(idx, Lnast_node::create_const("__synth_scope"));
  lnast->add_child(idx, Lnast_node::create_const("'" + livehd::synth_attr::encode(p) + "'"));
  attach_loc(idx, attrs);
}

void Prp2lnast::emit_attribute_list(const Lnast_node& target, TSNode attr_list_node) {
  // Two shapes feed this function:
  //   1. (legacy / read-side) `attribute_list` node: `[name (= value)?, ...]`.
  //      The grammar wraps each item in an anonymous `seq(name, value?)`, so
  //      `name` and `value` field tags appear directly on the parent.
  //   2. (new write-side) `tuple_sq` node holding `[key=value, ...]`. Each
  //      item is a named child — either `assignment` (key=value) or a bare
  //      identifier (flag-only, value defaults to `true`).
  for (const auto& [key, value] : synth_attribute_items(attr_list_node)) {
    auto idx = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(idx, target);
    lnast->add_child(idx, Lnast_node::create_const("synth." + key));
    lnast->add_child(idx, expr_to_node(value));
    attach_loc(idx, value);
  }
  std::string_view nt(ts_node_type(attr_list_node));
  // `tuple_sq` (legacy) and `attribute_sq` (current grammar, commit 93ca079+)
  // share the same item layout — only the item-node type names differ.
  // tuple_sq holds `assignment` items (lvalue may be `typed_identifier`);
  // attribute_sq holds `attribute_assignment` items (lvalue is a plain
  // `identifier`). Both also accept bare identifier/ref_identifier flags.
  if (nt == "tuple_sq" || nt == "attribute_sq") {
    for (TSNode item : ts_node_named_children(attr_list_node)) {
      std::string_view it(ts_node_type(item));
      if (it == "assignment" || it == "attribute_assignment") {
        TSNode lv = child_by_field(item, "lvalue");
        TSNode rv = child_by_field(item, "rvalue");
        if (ts_node_is_null(lv)) {
          continue;
        }
        // Strip an optional `:Type` wrapper on the attribute key (only
        // applies to legacy `assignment` items; `attribute_assignment`
        // already has a bare identifier lvalue).
        std::string_view key_txt;
        std::string_view lvt(ts_node_type(lv));
        if (lvt == "typed_identifier") {
          TSNode id = child_by_field(lv, "identifier");
          if (!ts_node_is_null(id)) {
            key_txt = trim(get_text(id));
          }
        } else {
          key_txt = trim(get_text(lv));
        }
        if (key_txt.empty()) {
          continue;
        }
        reject_common_mistakes_attr_name(lv, key_txt, !ts_node_is_null(rv));
        check_attribute_value(item, key_txt, rv);
        auto idx = builder.add_child(Lnast_ntype::create_attr_set());
        lnast->add_child(idx, target);
        lnast->add_child(idx, Lnast_node::create_const(key_txt));
        if (!ts_node_is_null(rv)) {
          lnast->add_child(idx, expr_to_node(rv));
        } else {
          lnast->add_child(idx, Lnast_node::create_const("true"));
        }
        attach_loc(idx, item);  // span → semacheck's read-only-attr-write can point here
      } else if (it == "identifier" || it == "ref_identifier") {
        auto txt = trim(get_text(item));
        if (txt.empty()) {
          continue;
        }
        reject_common_mistakes_attr_name(item, txt, false);
        auto idx = builder.add_child(Lnast_ntype::create_attr_set());
        lnast->add_child(idx, target);
        lnast->add_child(idx, Lnast_node::create_const(txt));
        lnast->add_child(idx, Lnast_node::create_const("true"));
        attach_loc(idx, item);
      }
    }
    return;
  }
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
    reject_common_mistakes_attr_name(name_n, get_text(name_n), have_val);
    check_attribute_value(name_n, get_text(name_n), have_val ? val_n : TSNode{});
    auto idx = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(idx, target);
    lnast->add_child(idx, Lnast_node::create_const(get_text(name_n)));
    if (have_val) {
      lnast->add_child(idx, expr_to_node(val_n));
    } else {
      lnast->add_child(idx, Lnast_node::create_const("true"));
    }
    attach_loc(idx, name_n);  // span → semacheck's read-only-attr-write can point here
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
    std::string_view kind;    // points into ts_node_type's static string for the inner aliased op
    TSNode           node{};  // the binary_*_op wrapper node, for diagnostic spans
  };
  std::vector<Lnast_node> operands;
  std::vector<TSNode>     operand_nodes;  // parallel to `operands`, source TSNodes (for precedence checks)
  std::vector<Op>         ops;
  operands.reserve(nnc / 2 + 1);
  operand_nodes.reserve(nnc / 2 + 1);
  ops.reserve(nnc / 2);
  for (TSNode c : ts_node_named_children(n)) {
    std::string_view ct(ts_node_type(c));
    Tier             tier = Tier::unknown;
    if (ct == "binary_times_op") {
      tier = Tier::times;
    } else if (ct == "binary_other_op") {
      tier = Tier::other;
    } else if (ct == "binary_step_op") {
      // `a..=b step n`: a separate (looser) grammar precedence, but functionally
      // an "other"-tier op — emit_other has the `op_step` → step() call case.
      // It is always the single outer op over a (range) left operand, so it
      // never mixes with arithmetic at this level.
      tier = Tier::other;
    } else if (ct == "binary_compare_op") {
      tier = Tier::compare;
    } else if (ct == "binary_logical_op") {
      tier = Tier::logical;
    }
    if (tier != Tier::unknown) {
      // The wrapper has a single named child of the aliased op kind.
      TSNode inner = ts_node_named_child(c, 0);
      ops.push_back({tier, ts_node_is_null(inner) ? std::string_view{} : std::string_view(ts_node_type(inner)), c});
    } else {
      operand_nodes.push_back(c);
    }
  }

  // Convert operands AFTER the op kinds are known: an operand adjacent to a
  // `does`/`equals`/`case` operator is in type position — a primitive
  // type token there is a type literal, not a variable read. Conversion stays
  // in source order, so helper-stmt emission order is unchanged.
  auto is_type_pos_op = [](std::string_view k) {
    return k == "op_does" || k == "op_not_does" || k == "op_case" || k == "op_not_case" || k == "op_equals" || k == "op_not_equals";
  };
  for (size_t j = 0; j < operand_nodes.size(); ++j) {
    const bool type_pos
        = (j > 0 && j - 1 < ops.size() && is_type_pos_op(ops[j - 1].kind)) || (j < ops.size() && is_type_pos_op(ops[j].kind));
    if (!type_pos) {
      check_lambda_used_as_value(operand_nodes[j]);
    }
    operands.push_back(type_pos ? does_operand_to_node(operand_nodes[j]) : expr_to_node(operand_nodes[j]));
  }

  if (ops.empty()) {
    return operands.front();
  }
  I(operands.size() == ops.size() + 1);

  // --- Precedence-mixing check (docs/docs/pyrope/04-variables.md "Precedence").
  // Pyrope has very shallow precedence: parentheses may be omitted only when an
  // expression evaluated left-to-right gives the same result as right-to-left.
  // The grammar flattens each priority tier into one chain, so the operator
  // structure that proves (or disproves) order-independence survives only here —
  // lowering below collapses it into nested tmp refs. Flag the ambiguous mixes
  // now, while we still can.
  //
  //   tier 3 "other" (+ - ++ << >> & | ^ !& !| !^ ..= ..< ..+ step):
  //     distinct operators may be mixed only when they are all additive (+,-)
  //     or all the same associative operator (& | ^ ++). In addition, a bare
  //     mult/div operand (`a*b`, not the parenthesized `(a*b)`) may sit only
  //     next to + or - ("mult/div precedence is only against +,- operators").
  //   tier 5 "logical" (and or implies): distinct operators may not be mixed.
  {
    auto is_additive = [](std::string_view k) { return k == "op_add" || k == "op_sub"; };
    auto is_assoc_other
        = [](std::string_view k) { return k == "op_bit_and" || k == "op_bit_or" || k == "op_bit_xor" || k == "op_tuple_concat"; };
    // An unparenthesized priority-2 (`*` `/` `%`) operand. `(a*b)` parses as a
    // `tuple`, not an `expression_item`, so parentheses make this return false.
    auto is_bare_times = [](TSNode c) {
      if (std::string_view(ts_node_type(c)) != "expression_item") {
        return false;
      }
      for (TSNode tc : ts_node_named_children(c)) {
        if (std::string_view(ts_node_type(tc)) == "binary_times_op") {
          return true;
        }
      }
      return false;
    };

    const Tier tier = ops.front().tier;  // a chain is homogeneous: one grammar tier
    if (tier == Tier::other) {
      // mult/div precedence is defined only against +/-.
      for (size_t i = 0; i < operand_nodes.size(); ++i) {
        if (!is_bare_times(operand_nodes[i])) {
          continue;
        }
        bool left_bad  = i > 0 && !is_additive(ops[i - 1].kind);
        bool right_bad = i < ops.size() && !is_additive(ops[i].kind);
        if (left_bad || right_bad) {
          report_error(left_bad ? ops[i - 1].node : ops[i].node,
                       "mixed-precedence",
                       "syntax",
                       "mult/div precedence is defined only against `+`/`-`: add parentheses around the "
                       "`*`/`/` sub-expression to make the precedence explicit",
                       "wrap the multiply/divide in parentheses, e.g. `a & (b * c)`");
        }
      }
      // Distinct same-tier operators only mix when all additive or all the same
      // associative operator.
      if (ops.size() >= 2) {
        bool all_additive = true;
        bool all_same     = true;
        for (const auto& o : ops) {
          all_additive = all_additive && is_additive(o.kind);
          all_same     = all_same && (o.kind == ops.front().kind);
        }
        if (!all_additive && !(all_same && is_assoc_other(ops.front().kind))) {
          // Blame the first operator that breaks homogeneity (or the second
          // operator for a repeated non-associative one like `<<`).
          size_t blame = 1;
          for (size_t i = 1; i < ops.size(); ++i) {
            if (ops[i].kind != ops.front().kind) {
              blame = i;
              break;
            }
          }
          report_error(ops[blame].node,
                       "mixed-precedence",
                       "syntax",
                       "operators at the same precedence cannot be mixed without parentheses — the result "
                       "depends on evaluation order; add parentheses to make the grouping explicit",
                       "add parentheses to group the sub-expressions");
        }
      }
    } else if (tier == Tier::logical && ops.size() >= 2) {
      for (size_t i = 1; i < ops.size(); ++i) {
        if (ops[i].kind != ops.front().kind) {
          report_error(ops[i].node,
                       "mixed-precedence",
                       "syntax",
                       "logical operators `and`/`or`/`implies` at the same precedence cannot be mixed "
                       "without parentheses; add parentheses to make the grouping explicit",
                       "add parentheses, e.g. `(x or y) and z`");
        }
      }
    }
  }

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
  // still used by ops without a dedicated LNAST ntype (step). `implies` no
  // longer uses it; it lowers to `!a or b` (log_not + log_or) in emit_logical.
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
    // `!in` was removed from the grammar (`a !in b` is now a syntax error), so
    // there is no `op_not_in` compare-op branch. `op_is` / `op_not_is` were
    // likewise removed (task 1f-is_typename,
    // tree-sitter-pyrope daf3508): `a is b` is now a syntax error, so there is
    // no compare-op branch for it. Structural `does` / `equals` / `case` remain.
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

  // Range nodes carry a source span (attach_loc) so constprop's
  // descending-range diagnostic can point at the offending `a..=b`. The
  // operator wrapper TSNode is threaded in via emit_other's `opn`.
  auto make_range = [&](const Lnast_node& l, const Lnast_node& r, const TSNode& opn) {
    auto idx = builder.add_child(Lnast_ntype::create_range());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, l);
    lnast->add_child(idx, r);
    attach_loc(idx, opn);
    return ref;
  };

  auto emit_other = [&](std::string_view kind, const Lnast_node& l, const Lnast_node& r, const TSNode& opn) -> Lnast_node {
    if (kind == "op_add") {
      return make_binop(Lnast_ntype::create_plus(), l, r);
    }
    if (kind == "op_sub") {
      return make_binop(Lnast_ntype::create_minus(), l, r);
    }
    if (kind == "op_tuple_concat") {
      // Like make_range: the concat node carries a source span so constprop's
      // leaf-overlap diagnostic can point at the `a ++ b` site.
      auto idx = builder.add_child(Lnast_ntype::create_tuple_concat());
      auto ref = builder.mint_tmp_ref();
      lnast->add_child(idx, ref);
      lnast->add_child(idx, l);
      lnast->add_child(idx, r);
      attach_loc(idx, opn);
      return ref;
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
      return make_range(l, r, opn);
    }
    if (kind == "op_range_exclusive") {
      auto m    = builder.add_child(Lnast_ntype::create_minus());
      auto mref = builder.mint_tmp_ref();
      lnast->add_child(m, mref);
      lnast->add_child(m, r);
      lnast->add_child(m, Lnast_node::create_const("1"));
      return make_range(l, mref, opn);
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
      return make_range(l, mref, opn);
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
      // `a implies b` is boolean implication: false only when a is true and b
      // is false. Lgraph has no implies cell, so lower it to the equivalent
      // `!a or b` here (same shape as op_log_nand/op_log_nor above). The
      // log_not must be emitted before the log_or that consumes it, so bind it
      // to a local rather than relying on argument evaluation order.
      auto not_l = wrap_not(l);
      return make_binop(Lnast_ntype::create_log_or(), not_l, r);
    }
    if (kind == "op_not_implies") {
      // `!(a implies b)` ≡ `!(!a or b)`. Defensive: tier-5 `binary_logical_op`
      // only spells `and`/`or`/`implies`, so the grammar never emits `!implies`.
      auto not_l = wrap_not(l);
      return wrap_not(make_binop(Lnast_ntype::create_log_or(), not_l, r));
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

  // Logical-tier mixing (`a and b or c`) was already rejected by the
  // precedence-mixing check above, so the chain here is homogeneous.

  // All other tiers: left-fold.
  Lnast_node acc = operands.front();
  for (size_t i = 0; i < ops.size(); ++i) {
    switch (ops[i].tier) {
      case Tier::times  : acc = emit_times(ops[i].kind, acc, operands[i + 1]); break;
      case Tier::other  : acc = emit_other(ops[i].kind, acc, operands[i + 1], ops[i].node); break;
      case Tier::logical: acc = emit_logical(ops[i].kind, acc, operands[i + 1]); break;
      default           : std::print("prp2lnast: unknown op kind `{}`\n", ops[i].kind); return builder.mint_tmp_ref();
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
    if (std::string_view(ts_node_type(ts_node_is_null(op_n) ? arg_n : op_n)) != "op_spread") {
      check_lambda_used_as_value(arg_n);
    }
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

Lnast_node Prp2lnast::if_expr_to_node(TSNode n, bool need_result) {
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
  //
  // need_result == false when the if is used as a statement (its value is
  // discarded); skip the per-arm placeholder `assign result = 0` and the
  // result tmp entirely.
  Lnast_node result;
  if (need_result) {
    result = builder.mint_tmp_ref();
  }

  Initializer_scope_guard init_scope(*this, n);

  struct Arm {
    Lnast_node cref;
    TSNode     code;
  };
  std::vector<Arm> arms;
  TSNode           else_code{};
  bool             have_else = false;

  // `_if_branch` is inlined into `if_expression`, so the leading `if` and every
  // `elif` each contribute their own `init` field (flattened, in source order
  // `init, condition, code` per branch). Lower each branch's init where it is
  // encountered — before that branch's condition — so an elif header decl
  // (`elif mut x2 = …; x2 == …`) is visible to its own condition. (The old
  // once-only `eval_init` lowered only the first branch's init.)
  auto lower_branch_init = [&](TSNode init) {
    if (ts_node_is_null(init)) {
      return;
    }
    for (TSNode c : ts_node_named_children(init)) {
      process_statement(c);
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
    if (f == "init") {
      // This branch's header decls (`if/elif mut x = …; cond`): lower now so
      // they precede this branch's condition read.
      lower_branch_init(c);
    } else if (f == "condition") {
      pending_cond = c;
      have_cond    = true;
    } else if (f == "code") {
      if (in_else) {
        else_code = c;
        have_else = true;
        in_else   = false;
      } else if (have_cond) {
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

  // `unique if`: the optional leading `unique` keyword is an anonymous
  // (field-less) child of if_expression — the field loop above skips it, so
  // scan for it directly. A unique if declares the arm conditions mutually
  // exclusive (an implicit assume) and lowers to a distinct `unique_if`
  // LNAST node, which tolg turns into a Hotmux instead of a Mux chain.
  bool is_unique = false;
  for (uint32_t i = 0; i < nc; i++) {
    if (trim(get_text(child(n, i))) == "unique") {  // anonymous `unique` marker (text, not node type)
      is_unique = true;
      break;
    }
  }

  // All cond stmts (and init) have now been emitted to builder.idx_stmts. Add the
  // `if` here so it follows its producers in source order.
  auto if_idx = builder.add_child(is_unique ? Lnast_ntype::create_unique_if() : Lnast_ntype::create_if());
  attach_loc(if_idx, n);  // span → upass/typecheck cond-not-bool can point here

  // Set of named-child node kinds that are expression-typed and can serve
  // as the value of an arm body. Mirrors `expr_stmt` in process_statement.
  static const absl::flat_hash_set<std::string_view> arm_expr_kinds = {
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
      "paren_group",
      "attribute_set",
      "timed_identifier",
      "constant",
  };

  auto emit_body = [&](TSNode code) {
    auto body_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
    builder.push_stmts(body_idx);
    // When the if is used as an expression, the arm body's value is its
    // last expression. Detect a trailing expression-typed named child and
    // emit `assign result <expr>` for it; preceding statements process as
    // normal. Without this, every arm assigns 0 and the if-expression
    // always folds to 0 instead of the body's computed value.
    bool emitted_result = false;
    if (need_result && !ts_node_is_null(code) && std::string_view(ts_node_type(code)) == "scope_statement") {
      uint32_t nnc = ts_node_named_child_count(code);
      if (nnc > 0) {
        TSNode           last = ts_node_named_child(code, nnc - 1);
        std::string_view lt(ts_node_type(last));
        if (arm_expr_kinds.contains(lt)) {
          // Process the leading statements (everything before the last
          // expression) by walking the children in source order, skipping
          // the trailing expression which we will lower as a value.
          for (uint32_t i = 0; i < ts_node_child_count(code); i++) {
            TSNode c = ts_node_child(code, i);
            if (!ts_node_is_named(c)) {
              continue;
            }
            if (ts_node_start_byte(c) == ts_node_start_byte(last) && ts_node_end_byte(c) == ts_node_end_byte(last)) {
              break;  // reached the trailing expression — handle below
            }
            process_statement(c);
          }
          Lnast_node val  = expr_to_node(last);
          auto       aidx = builder.add_child(Lnast_ntype::create_store());
          lnast->add_child(aidx, result);
          lnast->add_child(aidx, val);
          emitted_result = true;
        }
      }
    }
    if (!emitted_result) {
      if (!ts_node_is_null(code)) {
        process_scope_statement(code, body_idx);
      }
      if (need_result) {
        auto a = builder.add_child(Lnast_ntype::create_store());
        lnast->add_child(a, result);
        lnast->add_child(a, Lnast_node::create_const("0"));
      }
    }
    builder.pop_stmts();
  };

  for (const auto& arm : arms) {
    lnast->add_child(if_idx, arm.cref);
    emit_body(arm.code);
  }
  if (have_else) {
    emit_body(else_code);
  }

  return need_result ? result : Lnast_node::create_const("0");
}

Lnast_node Prp2lnast::match_expr_to_node(TSNode n, bool need_result) {
  // match init?; subject { (operator? expr_list | else) { code } ... }
  // Expand to a chain of unique if/else: each arm's per-pattern compare
  // emits the operator-appropriate node (`eq` for `==` / default,
  // `func_case` for `case`, negated counterparts for `!=` / `!case`).
  // Multi-pattern arms collapse via `log_or`. The `init` clause (e.g.
  // `match const t = m1; t { … }`) is processed *before* the conds so
  // any local binding it introduces is visible to the arm bodies.

  // Mint the `result` ref (when consumed) BEFORE wrapping in the match's
  // private stmts. Tmps live at function scope so they survive the wrap
  // exit, but doing this here also lets the caller reference `result`
  // outside the wrap.
  Lnast_node result_outer;
  if (need_result) {
    result_outer = builder.mint_tmp_ref();
  }

  Initializer_scope_guard init_scope(*this, n);

  TSNode init = child_by_field(n, "init");
  if (!ts_node_is_null(init)) {
    for (TSNode c : ts_node_named_children(init)) {
      process_statement(c);
    }
  }

  TSNode subject   = {};
  TSNode else_code = {};
  bool   have_subj = false;
  // Find subject: the first 'condition' field child whose node type is
  // not an op-token or the 'else' keyword (tree-sitter tags every child
  // of the seq with the field name — see if_expr_to_node).
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
  // arm first, emit each arm's compare/log_or compute stmts to builder.idx_stmts,
  // collect (cref, code) pairs, THEN add the if and assemble. Otherwise
  // every compare lands as a sibling AFTER the if and the if's cond refs are
  // dangling once constprop drops the helpers.
  // need_result == false when the match is used as a statement (its value is
  // discarded); skip the per-arm placeholder `assign result = 0` and the
  // result tmp entirely. The result tmp itself was minted above before the
  // wrap stmts, so the caller's reference outlives the match scope.
  Lnast_node result = result_outer;

  struct Arm {
    Lnast_node cref;
    TSNode     code;
  };
  std::vector<Arm> arms;

  bool             saw_first_condition = false;
  TSNode           pending_expr        = {};
  bool             have_pending        = false;
  bool             pending_is_else     = false;
  std::string_view pending_op          = "==";  // default per pyrope `match`
  for (uint32_t i = 0; i < child_count(n); i++) {
    const char* f = ts_node_field_name_for_child(n, i);
    if (!f) {
      continue;
    }
    std::string_view field(f);
    TSNode           c = child(n, i);
    if (field == "else_code") {
      // New grammar: the else body is its own field on match_expression.
      else_code = c;
      continue;
    }
    if (field == "condition") {
      if (!saw_first_condition) {
        saw_first_condition = true;
        continue;
      }
      std::string_view t(ts_node_type(c));
      std::string_view txt = trim(get_text(c));
      if (txt == "else") {
        // Old grammar: 'else' keyword tagged with the same "condition" field.
        pending_is_else = true;
      } else if (!ts_node_is_named(c)) {
        // Anonymous op-token preceding the arm RHS (e.g. 'case', '!=', '<').
        // Tree-sitter tags it with the same "condition" field because the
        // grammar uses field('condition', seq(optional(op), _expression)).
        // Remember it so the next arm-RHS knows which compare to emit.
        pending_op = txt;
      } else if (t == "expression_list" || t == "expression_item" || t == "constant" || t == "identifier" || t == "tuple"
                 || t == "tuple_sq" || t == "paren_group" || t == "function_call_expression" || t == "member_selection"
                 || t == "bit_selection" || t == "dot_expression" || t == "unary_expression" || t == "attribute_read"
                 || t == "attribute_set" || t == "if_expression" || t == "match_expression") {
        // Any expression-typed named child is the arm RHS.
        pending_expr = c;
        have_pending = true;
      } else {
        // Unknown — treat as an op-token text.
        pending_op = txt;
      }
    } else if (field == "code") {
      if (pending_is_else) {
        else_code       = c;
        pending_is_else = false;
      } else if (have_pending) {
        const bool use_case     = (pending_op == "case" || pending_op == "!case");
        // Structural / membership arm operators get their dedicated ops —
        // `does Person { … }` must lower to func_does (a superset receiver
        // satisfies `does` but never `eq`), `in (…)` to func_in.
        const bool use_does     = (pending_op == "does" || pending_op == "!does");
        const bool use_in       = (pending_op == "in");  // `!in` was removed from the grammar
        const bool negate       = (pending_op == "!case" || pending_op == "!=" || pending_op == "!does");
        // Emit `<compare> tmp = subject_ref rhs` and return the tmp ref,
        // wrapping in log_not when the operator is the negated form.
        auto       emit_compare = [&](const Lnast_node& rhs) {
          // Relational arm ops (`< 0`, `>= 5`, …) must lower to their proper
          // relational node — they previously fell through to `eq`, silently
          // turning `< rhs` into `== rhs`. Operand order is (subject, rhs).
          auto compare_ntype = use_case             ? Lnast_ntype::create_func_case()
                                     : use_does           ? Lnast_ntype::create_func_does()
                                     : use_in             ? Lnast_ntype::create_func_in()
                                     : pending_op == "<"  ? Lnast_ntype::create_lt()
                                     : pending_op == "<=" ? Lnast_ntype::create_le()
                                     : pending_op == ">"  ? Lnast_ntype::create_gt()
                                     : pending_op == ">=" ? Lnast_ntype::create_ge()
                                                          : Lnast_ntype::create_eq();
          auto idx           = builder.add_child(compare_ntype);
          auto ref           = builder.mint_tmp_ref();
          lnast->add_child(idx, ref);
          lnast->add_child(idx, subject_ref);
          lnast->add_child(idx, rhs);
          if (!negate) {
            return ref;
          }
          auto not_idx = builder.add_child(Lnast_ntype::create_log_not());
          auto neg_ref = builder.mint_tmp_ref();
          lnast->add_child(not_idx, neg_ref);
          lnast->add_child(not_idx, ref);
          return neg_ref;
        };

        // `does`/`case` arm patterns are in type position: a primitive
        // type token (`does u32 { … }`) is a type literal, not a variable.
        auto arm_rhs_to_node = [&](TSNode rhs) { return (use_does || use_case) ? does_operand_to_node(rhs) : expr_to_node(rhs); };
        Lnast_node       arm_cond;
        // Legacy grammar emitted an `expression_list` wrapper (multi-RHS arms);
        // the new grammar exposes the bare expression directly. Handle both.
        std::string_view pet(ts_node_type(pending_expr));
        if (pet == "expression_list") {
          uint32_t nnc = ts_node_named_child_count(pending_expr);
          if (nnc == 0) {
            arm_cond = emit_compare(constant_text_to_node(trim(get_text(pending_expr))));
          } else if (nnc == 1) {
            arm_cond = emit_compare(arm_rhs_to_node(ts_node_named_child(pending_expr, 0)));
          } else {
            auto or_idx = builder.add_child(Lnast_ntype::create_log_or());
            arm_cond    = builder.mint_tmp_ref();
            lnast->add_child(or_idx, arm_cond);
            for (TSNode rhs_n : ts_node_named_children(pending_expr)) {
              lnast->add_child(or_idx, emit_compare(arm_rhs_to_node(rhs_n)));
            }
          }
        } else {
          arm_cond = emit_compare(arm_rhs_to_node(pending_expr));
        }
        arms.push_back({arm_cond, c});
        have_pending = false;
        pending_op   = "==";  // reset for the next arm
      }
    }
  }

  // All compare/log_or compute stmts have been emitted to builder.idx_stmts.
  // Add the `if` here so it follows its producers in source order. A match
  // is by definition a unique-if chain (unique parallel case) — emit the
  // `unique_if` node so tolg lowers it to a Hotmux.
  auto if_idx = builder.add_child(Lnast_ntype::create_unique_if());

  // Same expression-typed arm-body lowering as if_expr_to_node: when the
  // match's value is consumed (need_result==true), detect a trailing
  // expression-typed named child and emit `assign result <expr>` for it;
  // preceding statements process as normal. Without this, every arm
  // assigns 0 and the match-expression always folds to 0 instead of the
  // body's computed value.
  static const absl::flat_hash_set<std::string_view> arm_expr_kinds = {
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
      "paren_group",
      "attribute_set",
      "timed_identifier",
      "constant",
  };

  auto emit_body = [&](TSNode code) {
    auto body_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
    builder.push_stmts(body_idx);
    bool emitted_result = false;
    if (need_result && !ts_node_is_null(code) && std::string_view(ts_node_type(code)) == "scope_statement") {
      uint32_t nnc = ts_node_named_child_count(code);
      if (nnc > 0) {
        TSNode           last = ts_node_named_child(code, nnc - 1);
        std::string_view lt(ts_node_type(last));
        if (arm_expr_kinds.contains(lt)) {
          for (uint32_t i = 0; i < ts_node_child_count(code); i++) {
            TSNode cc = ts_node_child(code, i);
            if (!ts_node_is_named(cc)) {
              continue;
            }
            if (ts_node_start_byte(cc) == ts_node_start_byte(last) && ts_node_end_byte(cc) == ts_node_end_byte(last)) {
              break;  // reached the trailing expression — handle below
            }
            process_statement(cc);
          }
          Lnast_node val  = expr_to_node(last);
          auto       aidx = builder.add_child(Lnast_ntype::create_store());
          lnast->add_child(aidx, result);
          lnast->add_child(aidx, val);
          emitted_result = true;
        }
      }
    }
    if (!emitted_result) {
      if (!ts_node_is_null(code)) {
        process_scope_statement(code, body_idx);
      }
      if (need_result) {
        auto a = builder.add_child(Lnast_ntype::create_store());
        lnast->add_child(a, result);
        lnast->add_child(a, Lnast_node::create_const("0"));
      }
    }
    builder.pop_stmts();
  };

  for (const auto& arm : arms) {
    lnast->add_child(if_idx, arm.cref);
    emit_body(arm.code);
  }
  if (!ts_node_is_null(else_code)) {
    emit_body(else_code);
  }

  return need_result ? result : Lnast_node::create_const("0");
}

// The operator of a `lo..=hi` / `lo..<hi` / `lo..+n` expression_item (its
// `op_range_*` node type), or "" when `expr_item` is not one of those forms.
// The operator is the aliased child of the binary_other_op wrapper, so the
// node type names it without reading text.
static std::string_view range_op_kind(TSNode expr_item) {
  if (ts_node_is_null(expr_item) || ts_node_named_child_count(expr_item) != 3) {
    return {};
  }
  TSNode op = ts_node_named_child(expr_item, 1);
  if (std::string_view(ts_node_type(op)) != "binary_other_op") {
    return {};
  }
  TSNode op_inner = ts_node_named_child(op, 0);
  if (ts_node_is_null(op_inner)) {
    return {};
  }
  const std::string_view kind(ts_node_type(op_inner));
  if (kind != "op_range_inclusive" && kind != "op_range_exclusive" && kind != "op_range_count") {
    return {};
  }
  return kind;
}

Lnast_node Prp2lnast::emit_range_node(const Lnast_node& start, const Lnast_node& end) {
  // Emit a `range` LNAST node (start, end) and return its ref. Used by the
  // open-end and from-zero forms of `#[...]`; constprop's process_get_mask /
  // process_set_mask consult range_map to synthesize the mask integer (closed
  // `lo..=hi` → bits-lo..hi mask; open `lo..` → -(1<<lo)).
  auto rng_idx = builder.add_child(Lnast_ntype::create_range());
  auto rng_ref = builder.mint_tmp_ref();
  lnast->add_child(rng_idx, rng_ref);
  lnast->add_child(rng_idx, start);
  lnast->add_child(rng_idx, end);
  return rng_ref;
}

std::vector<std::pair<Lnast_node, Lnast_node>> Prp2lnast::bit_selection_ranges(TSNode sel_node, int* const_width) {
  if (const_width) {
    *const_width = 0;
  }
  if (ts_node_is_null(sel_node) || ts_node_has_error(sel_node)) {
    report_error(sel_node, "bit-range-index", "syntax", "invalid bit selection", "use an index or an increasing range");
  }
  const auto zero = Lnast_node::create_const("0");
  const auto one  = Lnast_node::create_const("1");
  auto       add  = [&](const Lnast_node& x, const Lnast_node& y) {
    if (x.is_const() && y.is_const()) {
      return Lnast_node::create_const(Dlop::from_pyrope(x.get_name())->add_op(*Dlop::from_pyrope(y.get_name()))->to_pyrope());
    }
    return emit_bound_binop(Lnast_ntype::create_plus(), x, y);
  };
  auto bound = [&](TSNode n) {
    if (auto v = resolve_type_int_value(n)) {
      return Lnast_node::create_const(std::string(v->to_pyrope()));
    }
    return expr_to_node(n);
  };
  auto       index = child_by_field(sel_node, "index");
  const auto range = child_by_field(sel_node, "range");
  Lnast_node lo = zero, hi = Lnast_node::create_const("nil");
  int64_t    step = 1;
  if (!ts_node_is_null(index)) {
    if (ts_node_named_child_count(index) == 3
        && std::string_view(ts_node_type(ts_node_named_child(index, 1))) == "binary_step_op") {
      const auto amount = resolve_type_int_value(ts_node_named_child(index, 2));
      if (!amount || !amount->is_just_i64() || amount->has_unknowns() || amount->to_just_i64() <= 0) {
        report_error(index,
                     "bit-range-step",
                     "type",
                     "bit selection step must be a positive compile-time integer",
                     "use a positive constant step");
      }
      step  = amount->to_just_i64();
      index = ts_node_named_child(index, 0);
    }
    const auto kind = range_op_kind(index);
    if (kind.empty()) {
      lo = bound(index);
      hi = add(lo, one);
      if (const_width) {
        *const_width = 1;
      }
    } else {
      lo = bound(ts_node_named_child(index, 0));
      hi = bound(ts_node_named_child(index, 2));
      if (hi.is_const() && Dlop::from_pyrope(hi.get_name())->is_negative()) {
        report_error(sel_node,
                     "negative-bit-index",
                     "type",
                     "negative bit index is not allowed",
                     "bit/array/cycle indices are non-negative");
      }
      if (kind == "op_range_inclusive") {
        hi = add(hi, one);
      }
      if (kind == "op_range_count") {
        if (const_width && hi.is_const()) {
          auto width = Dlop::from_pyrope(hi.get_name());
          if (width->is_just_i64() && width->to_just_i64() > 0 && width->to_just_i64() <= std::numeric_limits<int>::max()) {
            *const_width = width->to_just_i64();
          }
        }
        hi = add(lo, hi);
      }
    }
  } else if (!ts_node_is_null(range)) {
    const auto from = child_by_field(range, "open_from");
    const auto incl = child_by_field(range, "from_zero_inclusive");
    const auto excl = child_by_field(range, "from_zero_exclusive");
    if (!ts_node_is_null(from)) {
      lo = bound(from);
    }
    if (!ts_node_is_null(incl)) {
      hi = add(bound(incl), one);
    }
    if (!ts_node_is_null(excl)) {
      hi = bound(excl);
    }
  }
  std::vector<std::pair<Lnast_node, Lnast_node>> result;
  const auto                                     lv = lo.is_const() ? Dlop::from_pyrope(lo.get_name()) : Dlop::nil();
  const auto                                     hv = hi.is_const() ? Dlop::from_pyrope(hi.get_name()) : Dlop::nil();
  if (lv->is_integer() && lv->is_negative()) {
    report_error(sel_node,
                 "negative-bit-index",
                 "type",
                 "negative bit index is not allowed",
                 "bit/array/cycle indices are non-negative");
  }
  if (lv->is_integer() && hv->is_integer() && !lv->has_unknowns() && !hv->has_unknowns()) {
    if (hv->le_op(*lv)->is_known_true()) {
      report_error(sel_node,
                   "descending-bit-range",
                   "type",
                   hv->eq_op(*lv)->is_known_true() ? "empty bit range is not allowed" : "descending bit range is not allowed",
                   "write the bounds low-to-high and select at least one bit");
    }
    if (lv->is_just_i64() && hv->is_just_i64() && hv->to_just_i64() <= std::numeric_limits<int>::max()) {
      const auto first = lv->to_just_i64(), last = hv->to_just_i64();
      if (const_width) {
        *const_width = static_cast<int>((last - first - 1) / step + 1);
      }
      if (step != 1) {
        for (auto bit = first; bit < last; bit += step) {
          result.emplace_back(Lnast_node::create_const(bit), Lnast_node::create_const(bit + 1));
        }
        return result;
      }
    }
  }
  if (step != 1) {
    report_error(sel_node,
                 "bit-range-step",
                 "unsupported",
                 "stepped bit selection needs compile-time bounds",
                 "use compile-time range bounds");
  }
  result.emplace_back(lo, hi);
  return result;
}

std::optional<Lnast_node> Prp2lnast::bit_range_lane_width(TSNode sel_node) {
  // A width that reads a runtime binding (a port, a `mut`, a runtime `const`
  // whose value this front end does not know) is not a compile-time width.
  // Every other name -- a comptime const, a generic, a loop index -- is left to
  // the runner, which folds it once the name has a value. So is an attribute
  // read: it is comptime whatever its base (`x.[bits]` of a port).
  auto reads_runtime = [&](TSNode e) {
    std::vector<TSNode> todo{e};
    while (!todo.empty()) {
      TSNode c = todo.back();
      todo.pop_back();
      if (ts_node_is_null(c)) {
        continue;
      }
      const std::string_view ct(ts_node_type(c));
      if (ct == "attribute_read") {
        continue;
      }
      if (ct == "identifier") {
        const auto hit = lookup_capture(canonical_escaped_ident(trim(get_text(c))));
        if (hit && hit->kind == Bind_kind::runtime && !hit->binding->int_value) {
          return true;
        }
        continue;
      }
      for (TSNode k : ts_node_named_children(c)) {
        todo.push_back(k);
      }
    }
    return false;
  };
  auto bound = [&](TSNode e) -> std::optional<Lnast_node> {
    if (ts_node_is_null(e)) {
      return std::nullopt;
    }
    if (const auto v = resolve_type_int_value(e)) {
      return Lnast_node::create_const(std::string(v->to_pyrope()));
    }
    if (reads_runtime(e)) {
      return std::nullopt;
    }
    return expr_to_node(e);
  };

  // (first, last) such that width = last - first + `inclusive`: the index
  // forms `lo..=hi` / `lo..<hi` / `lo..+n` and the from-zero `..=e` / `..<e`.
  TSNode index_n   = child_by_field(sel_node, "index");
  TSNode range     = child_by_field(sel_node, "range");
  TSNode lo_n      = {};
  TSNode hi_n      = {};
  bool   count     = false;
  bool   inclusive = false;
  if (!ts_node_is_null(index_n) && std::string_view(ts_node_type(index_n)) == "expression_item") {
    const std::string_view kind = range_op_kind(index_n);
    if (kind.empty()) {
      return std::nullopt;
    }
    lo_n      = ts_node_named_child(index_n, 0);
    hi_n      = ts_node_named_child(index_n, 2);
    count     = kind == "op_range_count";
    inclusive = kind == "op_range_inclusive";
  } else if (!ts_node_is_null(range)) {
    TSNode fz_incl = ts_node_child_by_field_name(range, "from_zero_inclusive", 19);
    TSNode fz_excl = ts_node_child_by_field_name(range, "from_zero_exclusive", 19);
    hi_n           = ts_node_is_null(fz_incl) ? fz_excl : fz_incl;
    inclusive      = !ts_node_is_null(fz_incl);
    if (ts_node_is_null(hi_n)) {
      return std::nullopt;  // `lo..` / `..`: the width depends on the base
    }
  } else {
    return std::nullopt;
  }

  const auto last = bound(hi_n);
  if (!last) {
    return std::nullopt;
  }
  std::optional<Lnast_node> first;
  if (!count && !ts_node_is_null(lo_n)) {
    first = bound(lo_n);
    if (!first) {
      return std::nullopt;
    }
  }
  const Lnast_node one = Lnast_node::create_const("1");
  if (last->is_const() && (!first || first->is_const())) {
    auto w = Dlop::from_pyrope(last->get_name());
    if (first) {
      w = w->sub_op(*Dlop::from_pyrope(first->get_name()));
    }
    if (inclusive) {
      w = w->add_op(*Dlop::create_integer(1));
    }
    return Lnast_node::create_const(std::string(w->to_pyrope()));
  }
  Lnast_node w = *last;
  if (first) {
    w = emit_bound_binop(Lnast_ntype::create_minus(), w, *first);
  }
  if (inclusive) {
    w = emit_bound_binop(Lnast_ntype::create_plus(), w, one);
  }
  return w;
}

Lnast_node Prp2lnast::bit_selection_to_node(TSNode n) {
  // bit_selection: argument '#' [reduction] select
  // LNAST get_mask expects a bitmask, not the selected bit position/range.
  TSNode arg = child_by_field(n, "argument");
  if (ts_node_is_null(arg)) {
    arg = ts_node_named_child(n, 0);
  }

  // A positional tuple literal used as a bit-selection base IS a bit packing:
  // `(a, b)#[..]` is the packed word with entry 0 at bit 0
  // (docs/pyrope/10-internals.md, "Bit selection and packing"). Emitting the
  // n-ary `concat` node here -- rather than a tuple value plus a get_mask --
  // is what gives every entry its DECLARED window: upass.tolg sizes each lane
  // from the lane's declared type and rejects an untyped one, which a tuple
  // VALUE cannot do, because a field carries a value and a value's magnitude
  // is exactly what a layout may not be sized by. It also means every modifier
  // (`#sext`, `#|`, `#+`, a sub-range) applies to the packed word for free:
  // they read whatever this base evaluates to.
  //
  // The lanes go out REVERSED. The LNAST concat node is MSB-first -- lane 0 is
  // the most significant window, the order inou.slang also produces for a
  // Verilog `{a, b}` -- while a packed tuple puts entry 0 at bit 0. Reversing
  // here, at the one producer that means the other order, keeps the node's
  // meaning single and leaves every Verilog-origin concat alone.
  //
  // Declined, so the ordinary tuple path still handles them:
  //   * a single item with NO trailing comma is parenthesization -- `(x+1)#[3]`
  //     must keep meaning a bit select of `x+1`, not a one-lane pack of an
  //     expression that has no declared width;
  //   * any NAMED or DECLARATION-shaped item -- the named rule (one field packs,
  //     two or more have no bit order) already lives on the bundle path, which
  //     sees the names, and a `(const 3, 4)` / `(x:u4, …)` field is a tuple
  //     declaration, not a positional pack.
  std::optional<Lnast_node> packed_base;
  if (std::string_view(ts_node_type(arg)) == "tuple") {
    auto is_spread = [&](TSNode c) {
      if (std::string_view(ts_node_type(c)) != "unary_expression") {
        return false;
      }
      TSNode op_n = child_by_field(c, "operator");
      return !ts_node_is_null(op_n) && std::string_view(ts_node_type(op_n)) == "op_spread";
    };
    std::vector<TSNode> items;
    bool                declined   = false;
    bool                has_spread = false;
    for (TSNode c : ts_node_named_children(arg)) {
      std::string_view t(ts_node_type(c));
      if (t == "comment") {
        // Comments are tree-sitter `extras`, so they surface as named children
        // even inside a tuple. Skip them -- counted as items they would both
        // turn `(x /*c*/)#[3]` into a two-lane pack and lower a comment into a
        // spurious lane, silently relocating every entry above it.
        continue;
      }
      if (t == "assignment" || t == "arg_assignment" || t == "typed_field" || t == "var_or_let_or_reg" || t == "lambda") {
        declined = true;
        break;
      }
      has_spread = has_spread || is_spread(c);
      items.push_back(c);
    }
    // A REAL trailing comma: text after the last item, the same test the tuple
    // grouping rule in expr_to_node uses. Scanning the child list for any `,`
    // would also fire on the separators of a multi-item tuple (harmless there,
    // but it says something the source does not).
    bool trailing_comma = false;
    if (!items.empty()) {
      const auto tail = text_between(ts_node_end_byte(items.back()), ts_node_end_byte(arg));
      trailing_comma  = tail.find(',') != std::string_view::npos;
    }
    if (!declined && !items.empty() && (items.size() > 1 || has_spread || trailing_comma)) {
      // Evaluate every entry in SOURCE order before minting the concat node, so
      // the statements each entry emits land ahead of it (and in the order the
      // source wrote them, which side effects depend on); only the LANE list is
      // reversed, below.
      std::vector<Lnast_node> vals;
      vals.reserve(items.size());
      for (TSNode c : items) {
        vals.push_back(expr_to_node(is_spread(c) ? child_by_field(c, "argument") : c));
      }
      auto cidx = builder.add_child(Lnast_ntype::create_concat());
      auto cref = builder.mint_tmp_ref();
      lnast->add_child(cidx, cref);
      // Every width goes out as the `nil` sentinel -- prp2lnast has no type
      // information, and a later pass binds each window from the entry's
      // declared type. A `...` spread contributes its INNER ref as one lane;
      // upass.tolg expands a declared array into its entries, entry 0 lowest.
      for (auto it = vals.rbegin(); it != vals.rend(); ++it) {
        lnast->add_child(cidx, *it);
        lnast->add_child(cidx, Lnast_node::create_const("nil"));
      }
      attach_loc(cidx, n);
      packed_base = cref;
    }
  }

  Lnast_node base = packed_base ? *packed_base : expr_to_node(arg);

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
  TSNode ext_node  = child_by_field(n, "extension");

  int        selected_width = 0;
  const auto ranges         = bit_selection_ranges(sel_node, &selected_width);
  if (packed_base && ts_node_is_null(type_node) && ts_node_is_null(ext_node) && ranges.size() == 1 && ranges[0].first.is_const()
      && ranges[0].first.get_name() == "0" && ranges[0].second.get_name() == "nil") {
    return *packed_base;
  }
  std::vector<Lnast_node> parts;
  for (const auto& [lo, hi] : ranges) {
    auto get  = builder.add_child(Lnast_ntype::create_get_mask());
    auto part = builder.mint_tmp_ref();
    lnast->add_child(get, part);
    lnast->add_child(get, base);
    lnast->add_child(get, lo);
    lnast->add_child(get, hi);
    parts.push_back(part);
  }
  auto ref = parts.front();
  if (parts.size() > 1) {
    auto cat = builder.add_child(Lnast_ntype::create_concat());
    ref      = builder.mint_tmp_ref();
    lnast->add_child(cat, ref);
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
      lnast->add_child(cat, *it);
      lnast->add_child(cat, Lnast_node::create_const("1"));
    }
  }
  if (selected_width > 0) {
    auto ts = builder.add_child(Lnast_ntype::create_type_spec());
    lnast->add_child(ts, ref);
    auto pt = lnast->add_child(ts, Lnast_ntype::create_prim_type_int());
    lnast->add_child(pt, Lnast_node::create_const(Dlop::get_mask_value(selected_width)->to_pyrope()));
    lnast->add_child(pt, Lnast_node::create_const("0"));
  }

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
      return ref;
    }
    auto r_idx = builder.add_child(red_node);
    auto r_ref = builder.mint_tmp_ref();
    lnast->add_child(r_idx, r_ref);
    lnast->add_child(r_idx, ref);
    return r_ref;
  }

  // Sign/zero extension (`#sext[lo..=hi]` / `#zext[lo..=hi]`). `get_mask`
  // already produces the selected bits as an unsigned value packed LSB-first
  // into positions 0..selected_width-1, which IS the zero-extended result.
  // For sext we additionally sign-extend from the top selected bit: the
  // packed slice's sign bit sits at position selected_width-1, so emit a
  // `sext` whose position operand is that index (constprop folds it via
  // Dlop::sext_op, matching the graph Sext cell).
  if (!ts_node_is_null(ext_node)) {
    std::string_view et(ts_node_type(ext_node));
    if (et == "sign_extend") {
      if (selected_width > 0) {
        auto sx  = builder.add_child(Lnast_ntype::create_sext());
        auto out = builder.mint_tmp_ref();
        lnast->add_child(sx, out);
        lnast->add_child(sx, ref);
        lnast->add_child(sx, Lnast_node::create_const(selected_width - 1));
        return out;
      } else {
        // Non-literal closed range `#sext[lo..=hi]`: the mask is a `range` ref
        // (non-const), so popcount isn't statically known — but the sign-bit
        // position equals `hi - lo` (= popcount(mask)-1 for a contiguous mask).
        // Emit `sext(ref, hi-lo)`; constprop folds the minus + sext once lo/hi
        // resolve. (Mirror bit_selection_ranges's inclusive-range detection.)
        TSNode idxn = child_by_field(sel_node, "index");
        if (!ts_node_is_null(idxn) && std::string_view(ts_node_type(idxn)) == "expression_item"
            && ts_node_named_child_count(idxn) == 3) {
          TSNode op_w = ts_node_named_child(idxn, 1);
          if (std::string_view(ts_node_type(op_w)) == "binary_other_op") {
            TSNode op_i = ts_node_named_child(op_w, 0);
            if (!ts_node_is_null(op_i) && std::string_view(ts_node_type(op_i)) == "op_range_inclusive") {
              Lnast_node lo_n    = expr_to_node(ts_node_named_child(idxn, 0));
              Lnast_node hi_n    = expr_to_node(ts_node_named_child(idxn, 2));
              auto       m_idx   = builder.add_child(Lnast_ntype::create_minus());
              auto       pos_ref = builder.mint_tmp_ref();
              lnast->add_child(m_idx, pos_ref);
              lnast->add_child(m_idx, hi_n);
              lnast->add_child(m_idx, lo_n);  // sign-bit position = hi - lo
              auto s_idx = builder.add_child(Lnast_ntype::create_sext());
              auto s_ref = builder.mint_tmp_ref();
              lnast->add_child(s_idx, s_ref);
              lnast->add_child(s_idx, ref);
              lnast->add_child(s_idx, pos_ref);
              return s_ref;
            }
          }
        }
      }
    }
    if (et == "sign_extend") {
      // Open selections have an infinite mask, but a finite declared source
      // window. The runner records the selected window on the get-mask result
      // after resolving scalar or aggregate layout; never popcount(-1).
      auto a_idx = builder.add_child(Lnast_ntype::create_attr_get());
      auto width = builder.mint_tmp_ref();
      lnast->add_child(a_idx, width);
      lnast->add_child(a_idx, ref);
      lnast->add_child(a_idx, Lnast_node::create_const("bits"));
      auto m_idx = builder.add_child(Lnast_ntype::create_minus());
      auto pos   = builder.mint_tmp_ref();
      lnast->add_child(m_idx, pos);
      lnast->add_child(m_idx, width);
      lnast->add_child(m_idx, Lnast_node::create_const("1"));
      auto s_idx = builder.add_child(Lnast_ntype::create_sext());
      auto s_ref = builder.mint_tmp_ref();
      lnast->add_child(s_idx, s_ref);
      lnast->add_child(s_idx, ref);
      lnast->add_child(s_idx, pos);
      return s_ref;
    }
    // get_mask already supplies the zero-extended result.
    return ref;
  }
  return ref;
}

Lnast_node Prp2lnast::member_selection_to_node(TSNode n) {
  TSNode     arg        = child_by_field(n, "argument");
  Lnast_node base       = expr_to_node(arg);
  // Collect field nodes first — each may emit auxiliary stmts (range, minus)
  // that must precede the tuple_get so single-pass constprop can resolve the
  // field's value before consuming it. Adding the tuple_get last keeps the
  // dependency order without a post-hoc reordering pass.
  auto       emit_range = [&](const Lnast_node& start, const Lnast_node& end) {
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

    TSNode index_n = child_by_field(sel, "index");
    if (!ts_node_is_null(index_n)) {
      fields.push_back(expr_to_node(index_n));
      continue;
    }

    TSNode range = child_by_field(sel, "range");
    if (!ts_node_is_null(range)) {
      TSNode open_all  = ts_node_child_by_field_name(range, "open_all", 8);
      TSNode open_from = ts_node_child_by_field_name(range, "open_from", 9);
      TSNode fz_incl   = ts_node_child_by_field_name(range, "from_zero_inclusive", 19);
      TSNode fz_excl   = ts_node_child_by_field_name(range, "from_zero_exclusive", 19);
      // `nil` is the open-end sentinel — round-trips through Dlop as a
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
      reject_common_mistakes_attr_name(name_n, get_text(name_n), false);
      if (const auto aname = get_text(name_n); (aname == "bw_max" || aname == "bw_min") && !in_assert_lowering_) {
        report_error(name_n,
                     "bw-attr-non-debug",
                     "type",
                     std::format("`.[{}]` is debug-only — readable only inside `cassert`/`assert`", aname),
                     "each elaboration may compute a different legal range; branch on `.[max]`/`.[min]` instead");
      }
      if (get_text(name_n) == "typename") {
        // Nominal type identity was removed (task 1f-is_typename): the
        // `typename` attribute is no longer a readable user attribute.
        report_error(name_n,
                     "typename-attr-removed",
                     "type",
                     "the `.[typename]` attribute has been removed",
                     "use the structural `does` / `equals` / `case` operators for type comparison");
      }
      auto idx = builder.add_child(Lnast_ntype::create_attr_get());
      auto ref = builder.mint_tmp_ref();
      lnast->add_child(idx, ref);
      lnast->add_child(idx, base);
      lnast->add_child(idx, Lnast_node::create_const(get_text(name_n)));
      base = ref;
    }
  }
  return base;
}

Lnast_node Prp2lnast::dot_expression_to_node(TSNode n) {
  // dot_expression: item ('.' identifier)+ — positional entries use `[N]`
  // (member_selection), never `.N`.
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
    // Canonicalize escaped field names (`` `in` `` → `in`) so the tuple_get key
    // matches the field as declared; a pure-alnum backtick name left unnormalized
    // is rejected downstream as an invalid ref.
    lnast->add_child(idx, Lnast_node::create_const(canonical_escaped_ident(trim(get_text(c)))));
  }
  return ref;
}

Lnast_node Prp2lnast::function_call_expr_to_node(TSNode n) {
  // `Unsigned(bits=4)` in value position is a TYPE literal, not a cast
  // (`Unsigned(bits=4).[max]` heads a suffix chain like `U4.[max]`).
  if (auto lit = int_type_literal(n)) {
    return *lit;
  }
  TSNode func = child_by_field(n, "function");
  TSNode arg  = child_by_field(n, "argument");  // tuple

  // Call-site attribute `callee::[name=X](args)` parses with the callee wrapped
  // in an `attribute_set` (parser.cpp made `attribute_set` callable). Peel it:
  // resolve the bare callee below, and remember name=X so it rides along as the
  // reserved `__inst_name` actual — consumed by the runner inliner as the
  // hierarchical prefix for the inlined regs/mems, or by tolg as the Sub
  // instance name for a non-inlined pipe/mod. Only `name` is supported today.
  std::string_view           callsite_inst_name;
  livehd::synth_attr::Policy callsite_synth;
  if (!ts_node_is_null(func) && std::string_view(ts_node_type(func)) == "attribute_set") {
    for (TSNode sq : ts_node_named_children(func)) {
      std::string_view sqt(ts_node_type(sq));
      if (sqt != "attribute_sq" && sqt != "tuple_sq") {
        continue;  // the `argument` child (the callee) — handled below
      }
      for (const auto& [key, value] : synth_attribute_items(sq)) {
        callsite_synth[key] = {livehd::synth_attr::literal(trim(get_text(value))), 3};
      }
      for (TSNode item : ts_node_named_children(sq)) {
        std::string_view it(ts_node_type(item));
        if (it != "assignment" && it != "attribute_assignment") {
          report_error(item,
                       "callsite-attribute",
                       "name",
                       "a call-site attribute must be `name=<instance>` — bare flags are not allowed here",
                       "write `f::[name=my_inst](args)`");
        }
        TSNode           lv  = child_by_field(item, "lvalue");
        TSNode           rv  = child_by_field(item, "rvalue");
        std::string_view key = ts_node_is_null(lv) ? std::string_view{} : trim(get_text(lv));
        if (key == "synth" || key.starts_with("synth.")) {
          continue;
        }
        if (key != "name") {
          report_error(item,
                       "callsite-attribute",
                       "name",
                       std::format("unsupported call-site attribute '{}' — only `name=<instance>` is allowed", key),
                       "the call-site `name` sets the instance/hierarchy name for the call");
        }
        if (ts_node_is_null(rv)) {
          report_error(item, "callsite-attribute", "name", "call-site `name` attribute needs a value: `name=<instance>`", "");
        }
        std::string_view v = trim(get_text(rv));
        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
          v = v.substr(1, v.size() - 2);  // accept a quoted form `name='my_inst'` / `name="my_inst"` too
        }
        callsite_inst_name = v;
      }
    }
    func = child_by_field(func, "argument");  // the bare callee
  }

  // Method-call form `receiver.method(args)`: the `function` field is a
  // `dot_expression` (receiver '.' identifier). Split it so the call lowers
  // to `fcall(method, receiver, ...args)` — otherwise the whole dotted path
  // becomes the function-name ref and the receiver disappears.
  Lnast_node func_ref;
  Lnast_node receiver_ref;
  bool       has_receiver = false;
  if (ts_node_is_null(func)) {
    func_ref = Lnast_node::create_const("nil");
  } else if (std::string_view(ts_node_type(func)) == "dot_expression" && ts_node_named_child_count(func) == 2
             && std::string_view(ts_node_type(ts_node_named_child(func, 0))) == "identifier"
             && trim(get_text(ts_node_named_child(func, 0))) == prp_builtins::std_namespace) {
    // `std.clog2(x)`: a function of the built-in `std` namespace, not a method
    // call on a value named `std`. It needs no import and lowers to a plain call
    // of the qualified name. A local binding named `std` would silently change
    // what the call means, so it is an error here rather than a shadow.
    const auto member = canonical_escaped_ident(trim(get_text(ts_node_named_child(func, 1))));
    if (find_binding(prp_builtins::std_namespace) != nullptr) {
      report_error(func,
                   "std-shadowed",
                   "name",
                   std::format("`std.{}` names the built-in `std` namespace, but a local binding `std` hides it here", member),
                   "rename the local `std`: `std` is the built-in namespace, visible in every file without an import");
    }
    if (!prp_builtins::is_std_member(member)) {
      report_error(func,
                   "std-unknown-member",
                   "name",
                   std::format("`std.{}` is not a function of the built-in `std` namespace", member),
                   "the built-in `std` namespace provides `std.clog2(x)`, `std.readmemh(file)`, and `std.readmemb(file)`");
    }
    if ((member == "testplusarg" || member == "valueplusarg") && simulation_test_depth_ == 0) {
      report_error(func,
                   "simulation-only-argument",
                   "type",
                   "simulation arguments may only be read inside a test block",
                   "pass stimulus through testbench DUT inputs; hardware debug argument reads are not supported yet");
    }
    func_ref = Lnast_node::create_ref(absl::StrCat(prp_builtins::std_namespace, ".", member));
  } else if (std::string_view(ts_node_type(func)) == "dot_expression" && ts_node_named_child_count(func) >= 2) {
    uint32_t fnc         = ts_node_named_child_count(func);
    TSNode   method_node = ts_node_named_child(func, fnc - 1);
    func_ref             = Lnast_node::create_ref(canonical_escaped_ident(trim(get_text(method_node))));
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
        lnast->add_child(tg_idx, Lnast_node::create_const(canonical_escaped_ident(trim(get_text(f)))));
      }
      receiver_ref = tg_ref;
    }
    has_receiver = true;
  } else {
    auto callee = trim(get_text(func));
    // A callee with a selector suffix (`` `past`[1] ``) carries its escaped head
    // inside a longer text, so canonicalize that head: `` `x` `` names x.
    if (callee.size() > 2 && callee.front() == '`' && callee.back() != '`') {
      if (const auto close = callee.find('`', 1); close != std::string_view::npos) {
        func_ref
            = Lnast_node::create_ref(absl::StrCat(canonical_escaped_ident(callee.substr(0, close + 1)), callee.substr(close + 1)));
      } else {
        func_ref = Lnast_node::create_ref(callee);
      }
    } else {
      func_ref = Lnast_node::create_ref(canonical_escaped_ident(callee));
    }
  }

  // `import("unit")` is a comptime builtin with its own canonical
  // lowering (validated string-literal argument; const-form callee).
  if (!has_receiver && func_ref.is_ref() && func_ref.get_name() == "import") {
    auto ref = builder.mint_tmp_ref();
    lower_import_call(n, arg, ref);
    return ref;
  }

  // Temporal builtin `past[N](x)` — `x` delayed by N clock cycles. Rewrite it,
  // at the source level, into the LNAST equivalent of
  //     stage[N] %past = x        // one depth-N pipeline register (N real flops)
  //     <result> = %past          // reads the stage q, which lands N cycles later
  // so it flows through the NORMAL stage/reg lowering — tolg finalize_regs wires
  // the clock/reset and realizes the shift register, and the interface `@[N]`
  // landing-cycle check is satisfied by the stage's feedforward σ. Only a literal
  // positive N is intercepted; every other callee falls through to the usual call
  // path. (A plain state `reg` chain would NOT work here: in a `mod`, plain regs
  // are cycle-0 state, so a q-read never advances the landing cycle — only a
  // feedforward `stage` shifts σ, which is what `@[N]` on the output requires.)
  if (!has_receiver && func_ref.is_ref()) {
    if (const uint64_t past_n = prp_builtins::past_builtin_delay(func_ref.get_name()); past_n != 0) {
      auto call_args = collect_call_args(arg);
      if (call_args.size() != 1 || call_args[0].is_assign || call_args[0].is_spread || call_args[0].is_ufcs) {
        report_error(n,
                     "past-arity",
                     "name",
                     "`past[N](x)` takes exactly one positional argument (the value to delay)",
                     "write `past[N](value)` — the result is `value` delayed by N clock cycles");
      }
      const std::string delay_txt(func_ref.get_name().substr(std::string_view("past[").size(),
                                                             func_ref.get_name().size() - std::string_view("past[").size() - 1));
      // Cluster head: attr_set(%past, "type", "stage") + trailing stages(N,N).
      // Identical shape to a source-level `stage[N] %past = x` (see the
      // declaration_statement handler), so the decl-merge folds it into a
      // `declare(%past, none, "reg", stages(N,N))` and tolg lowers one depth-N
      // pipeline Flop.
      auto              past_ref = builder.mint_tmp_ref();
      auto              as_idx   = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(as_idx, past_ref);
      lnast->add_child(as_idx, Lnast_node::create_const("type"));
      lnast->add_child(as_idx, Lnast_node::create_const("stage"));
      auto st = lnast->add_child(as_idx, Lnast_ntype::create_stages());
      lnast->add_child(st, Lnast_node::create_const(delay_txt));
      lnast->add_child(st, Lnast_node::create_const(delay_txt));
      attach_loc(as_idx, n);
      // The stage din: `%past = x`. Reads of %past (below and at the call site)
      // see its q.
      auto store_idx = builder.add_child(Lnast_ntype::create_store());
      lnast->add_child(store_idx, past_ref);
      lnast->add_child(store_idx, call_args[0].value);
      attach_loc(store_idx, n);
      return past_ref;
    }
  }

  // `concat(a, b, c)` was the positional bit-packing builtin -- what
  // SystemVerilog spells `{a, b, c}` -- and it was REMOVED
  // (docs/pyrope/21-deprecated.md). It read like `{a, b}` and was the only bit
  // spelling in Pyrope that ran high-to-low, while `#[0]` is the low bit, a bit
  // range is written low-to-high, and the string encoding puts the first
  // characters in the low bits. `(...)#[..]` covers what it did, in the
  // language's own direction.
  //
  // A hard error rather than a silent parse failure, and it must name the
  // ORDER: the migration is not a rename, the argument list REVERSES, so
  // anything mechanical that keeps the order miscompiles rather than fails.
  if (!has_receiver && func_ref.is_ref() && func_ref.get_name() == "concat") {
    report_error(n,
                 "concat-removed",
                 "type",
                 "`concat` was removed — a bit packing is written `(...)#[..]`",
                 "the argument order REVERSES, because entry 0 of a packing is at bit 0: `concat(a, b)` is "
                 "`(b, a)#[..]`. An argument that was a tuple or an array becomes a `...` splice, and its entries "
                 "reverse too — `concat(arr, x)` is `(x, arr[N-1], …, arr[0])#[..]`, or `(x, ...arr)#[..]` when the "
                 "natural entry-0-at-bit-0 order is what was wanted");
    return builder.mint_tmp_ref();
  }

  auto call_args    = collect_call_args(arg);
  auto generic_args = collect_generic_args(n);

  // Ruling 56 (docs 07-typesystem "Clock and Reset"): the one derived Clock is
  // a GATED one, a Clock construction with the named arguments `clock_pin`
  // and `enable` (`Clock(clock_pin=clk, enable=en)`), lowered by upass.tolg
  // to the LGraph Clock_cell (an ICG). A bare variable named like the
  // parameter binds by name as in any call. A positional `Clock(x)` is not a
  // cast and falls through to the undefined-read check (no-clock-reset-cast).
  const bool clock_gate = !has_receiver && func_ref.is_ref() && func_ref.get_name() == "Clock"
                          && std::ranges::any_of(call_args, [](const Call_arg& a) { return a.is_assign; });
  if (clock_gate) {
    bool has_clock  = false;
    bool has_enable = false;
    bool has_invert = false;
    bool bad        = !generic_args.empty();
    for (auto& a : call_args) {
      if (!a.is_assign && !a.is_ref && !a.is_spread && !a.is_ufcs && a.value.is_ref()) {
        a.is_assign  = true;  // `enable` for `enable=enable`
        a.assign_key = std::string(a.value.get_name());
      }
      bool* seen = a.assign_key == "clock_pin" ? &has_clock
                   : a.assign_key == "enable"  ? &has_enable
                   : a.assign_key == "invert"  ? &has_invert
                                               : nullptr;
      if (!a.is_assign || a.is_ref || seen == nullptr || *seen) {
        bad = true;
      } else {
        *seen = true;
      }
      // `invert` picks the gate flavour at compile time: a literal `true`/`false`.
      if (a.is_assign && a.assign_key == "invert"
          && !(a.value.is_const() && (a.value.get_name() == "true" || a.value.get_name() == "false"))) {
        bad = true;
      }
    }
    if (bad || !has_clock || !has_enable) {
      report_error(n,
                   "clock-gate-args",
                   "type",
                   "`Clock(...)` gates a clock and takes the named arguments `clock_pin` and `enable` (and optionally a literal "
                   "`invert=true`)",
                   "write `Clock(clock_pin=clk, enable=en)`: `clk` a `Clock`, `en` a `Bool` enable; "
                   "`invert=true` makes it an active-low gate");
    }
  }

  if (!has_receiver && (func_ref.get_name() == "std.readmemh" || func_ref.get_name() == "std.readmemb")) {
    if (call_args.size() != 1 || call_args[0].is_assign || call_args[0].is_spread || !generic_args.empty()) {
      report_error(n,
                   "readmem-filename",
                   "type",
                   "std.readmemh/readmemb requires one nonempty comptime string filename",
                   "use reg mem:[DEPTH]uWIDTH = std.readmemh(\"image.hex\")");
    }
    const bool hex = func_ref.get_name() == "std.readmemh";
    // Diagnostic source IDs intentionally discard absolute roots. Carry the
    // runtime file-resolution base explicitly, including through cached LNAST.
    Call_arg   source;
    source.value = Lnast_node::create_const(
        Dlop::create_string(
            hlop::memory_image_command(hex ? 16 : 2, std::filesystem::absolute(src_filename).parent_path().string()))
            ->to_pyrope());
    call_args.push_back(std::move(source));
    func_ref = Lnast_node::create_ref(hex ? "__readmemh" : "__readmemb");
  }
  // Tuple key introspection shares the named-field list with .[fields].
  if (has_receiver && func_ref.get_name() == "keys") {
    if (!call_args.empty() || !generic_args.empty()) {
      report_error(n, "keys-arity", "type", "`keys()` takes no arguments", "use `tuple.keys()` to list the named keys");
    }
    auto ref = builder.mint_tmp_ref();
    auto idx = builder.add_child(Lnast_ntype::create_attr_get());
    lnast->add_child(idx, ref);
    lnast->add_child(idx, receiver_ref);
    lnast->add_child(idx, Lnast_node::create_const("fields"));
    attach_loc(idx, n);
    return ref;
  }
  if (!callsite_synth.empty()) {
    Call_arg ia;
    ia.is_assign  = true;
    ia.assign_key = "__synth_call";
    ia.value      = Lnast_node::create_const("'" + livehd::synth_attr::encode(callsite_synth) + "'");
    call_args.push_back(std::move(ia));
  }
  if (!callsite_inst_name.empty()) {
    // Reserved actual: `store(__inst_name, const "X")`. Position-independent
    // (the runner / tolg match it by name and never bind it to a port).
    Call_arg ia;
    ia.is_assign  = true;
    ia.assign_key = std::string(call_inst_name_marker);
    ia.value      = Lnast_node::create_const(callsite_inst_name);
    call_args.push_back(std::move(ia));
  }
  if (has_receiver) {
    Call_arg receiver_arg;
    receiver_arg.value   = receiver_ref;
    receiver_arg.is_ufcs = true;  // UFCS receiver marker (no self ⇒ compile error)
    call_args.insert(call_args.begin(), receiver_arg);
  }

  auto idx = builder.add_child(Lnast_ntype::create_func_call());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, func_ref);
  add_generic_args_to_fcall(idx, generic_args);
  add_call_args_to_fcall(idx, call_args);
  append_streamed_capture_actuals(idx, func_ref.get_name());
  for (const auto& a : call_args) {
    if (a.value.is_ref() && (a.is_ref || (a.is_ufcs && ufcs_writes_receiver(func_ref.get_name())))) {
      note_call_write(a.value.get_name());
    }
  }
  attach_loc(idx, n);  // call-site span → upass argument-naming diagnostics point here

  // Validate the callee EXISTS (like a value/type read): a free call `foo(...)`
  // must name a defined `comb`/`mod`/`pipe`, a function-valued variable/param,
  // or a built-in. Record it as a call Read_site so check_undefined_reads
  // resolves it through the normal visibility/hoisting logic. EXCLUDED:
  //   * methods / UFCS (`x.f()`) — has_receiver; resolved via the receiver type,
  //   * built-ins (assert/cputs/import/casts/…) — the central whitelist,
  //   * signature contexts (default-value calls) — generic params resolve late.
  if (!has_receiver && func_ref.is_ref() && inflight_name_scopes_.empty() && !clock_gate) {
    const auto callee = func_ref.get_name();
    if (!callee.empty() && !prp_name_is_tmp(callee) && callee.find('.') == std::string::npos
        && !prp_builtins::is_builtin_callee(callee)) {
      const auto sp = ts_node_start_point(func);
      read_sites_.push_back(Read_site{.name       = std::string{callee},
                                      .start_byte = ts_node_start_byte(func),
                                      .end_byte   = ts_node_end_byte(func),
                                      .start_line = sp.row + 1,
                                      .start_col  = sp.column + 1,
                                      .end_line   = sp.row + 1,
                                      .end_col    = sp.column + 1 + static_cast<uint32_t>(callee.size()),
                                      .scope      = builder.idx_stmts,
                                      .before     = lnast->get_last_child(builder.idx_stmts),
                                      .is_call    = true});
    }
  }
  return ref;
}

Lnast_node Prp2lnast::tuple_to_node(TSNode n, bool /*is_square*/, bool field_types_on_target) {
  // Collected once: the `var_or_let_or_reg` case needs an i+1 lookahead and
  // generated tuple literals can be huge (per-index child fetch is a linear scan).
  std::vector<TSNode> kids;
  for (TSNode kc : ts_node_named_children(n)) {
    kids.push_back(kc);
  }
  const uint32_t nnc = static_cast<uint32_t>(kids.size());

  // Tuple scope (04-variables.md): "Tuple field initializers follow program
  // order and can read earlier tuple fields by name". Fields are not
  // statement-level declarations, so expose each field name to
  // identifier_to_node through an in-flight frame as soon as its value has
  // been lowered. RAII so report_error unwinds cleanly.
  struct Inflight_scope_guard {
    std::vector<std::vector<std::string>>& scopes;
    explicit Inflight_scope_guard(std::vector<std::vector<std::string>>& s) : scopes(s) { scopes.emplace_back(); }
    ~Inflight_scope_guard() { scopes.pop_back(); }
  };
  Inflight_scope_guard tuple_scope(inflight_name_scopes_);

  // Pre-compute sub-expressions so their LNAST statements emit BEFORE the
  // tuple_add that references them — constprop runs in textual order.
  struct Item {
    bool        is_assign = false;
    bool        is_spread = false;  // `...expr` — emit value as its own concat chunk
    std::string assign_key;
    Lnast_node  value;
    // Phase 3: per-field attribute decorations on the lvalue
    // (e.g. `b::[poison=99]=2`). Lowered after the tuple_add as a
    // `tuple_get` + `attr_set` pair so the override is observable on the
    // resulting tuple field.
    TSNode      attr_list_node{};
    bool        has_attr_list{false};
    // Phase 8 typesystem: per-field type annotation on the lvalue
    // (e.g. `(a:u4=3, b:u4=5)`). Lowered after the tuple_add as a
    // `tuple_get` + `type_spec` pair so `t.a.[bits]` resolves.
    TSNode      type_cast_node{};
    bool        has_type_cast{false};
    // Field declared with an explicit `mut` marker (`const t = (mut a:u4=1)`).
    // Lowered after the tuple_add as a `declare(tg_tmp, prim_type_none, mut)`
    // so the attributes pass records Decl_kind::mut_kind on the field path —
    // writes through a mut field of a const tuple are legal, not a rebind.
    bool        is_mut{false};
    // `mut c:U24` / `c:U24` without a value: a NAMED field declaration (of a tuple
    // type), even though it lowers as a positional slot.
    bool        declares_field{false};
  };
  std::vector<Item> items;
  items.reserve(nnc);
  // Track named-field keys so a bundle literal that repeats a field name is
  // rejected: "each tuple field must be unique" (docs/docs/pyrope/03-bundle.md
  // "Concatenate fields"). Concatenation must be a separate post-declaration
  // statement (`y.ff ++= 2`), never a second field in the literal.
  absl::flat_hash_set<std::string> seen_field_keys;
  for (uint32_t i = 0; i < nnc; i++) {
    TSNode           c = kids[i];
    std::string_view t(ts_node_type(c));
    if (t == "comment") {
      // Comments are tree-sitter `extras`, so they surface as named children
      // even inside a tuple. Skip them — otherwise the generic `else` branch
      // below would lower a comment into a spurious positional const field.
      continue;
    }
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
          auto value   = expr_to_node(arg_n);
          auto spread  = builder.add_child(Lnast_ntype::create_func_call());
          it.value     = builder.mint_tmp_ref();
          lnast->add_child(spread, it.value);
          lnast->add_child(spread, Lnast_node::create_ref("__fkind__tuple_spread"));
          lnast->add_child(spread, value);
          attach_loc(spread, c);
          items.push_back(std::move(it));
          continue;
        }
      }
    }
    if (t == "assignment") {
      Item it;
      it.is_assign = true;
      // Per-field storage marker: `mut a:u4=1` carries a `decl` field with a
      // mut_decl storage child. Captured so the post-tuple_add decoration
      // loop can record the field's mutability (see Item::is_mut).
      if (TSNode dn = child_by_field(c, "decl"); !ts_node_is_null(dn)) {
        TSNode storage = child_by_field(dn, "storage");
        if (!ts_node_is_null(storage) && std::string_view(ts_node_type(storage)) == "mut_decl") {
          it.is_mut = true;
        }
      }
      // A bundle-literal field may only be introduced with plain `=`. A
      // compound operator (`++=`, `+=`, …) here is a compile error: declare
      // the field with `=`, then mutate it with `name OP= …` as a separate
      // statement (the variable must be `mut`). The grammar makes the
      // `operator` field a required `assignment_operator` whose single named
      // child names the aliased op kind (`assign`, `assign_tuple_concat`, …).
      {
        TSNode op_node = child_by_field(c, "operator");
        if (!ts_node_is_null(op_node)) {
          TSNode           op_inner = ts_node_named_child(op_node, 0);
          std::string_view op_k     = ts_node_is_null(op_inner) ? std::string_view{} : std::string_view(ts_node_type(op_inner));
          if (!op_k.empty() && op_k != "assign") {
            report_error(c,
                         "bundle-compound-assign",
                         "syntax",
                         std::format("compound assignment `{}` is not allowed inside a bundle literal", trim(get_text(op_node))),
                         "set the field with `=` here, then concatenate/update it with a separate `name OP= …` statement");
          }
        }
      }
      TSNode                      lv = child_by_field(c, "lvalue");
      TSNode                      rv = child_by_field(c, "rvalue");
      // The lvalue may carry a type_cast (`(a:u4=1, …)` parses lv as a
      // `typed_identifier`). Strip it: the ref text must be a bare
      // identifier; otherwise lnastfmt rejects the LNAST.
      std::string_view            lvt2(ts_node_type(lv));
      // Phase 3 — capture any per-field attribute carrier (`b::[poison=99]=2`)
      // wherever the grammar attaches it so we can lower it as a post-tuple
      // attr_set. The attribute_sq lives under the `typed_identifier`'s
      // type_cast (or on the assignment node itself); walk the lvalue
      // subtree to find it.
      // Recursively walk the lvalue subtree (and the assignment node itself,
      // since the grammar may attach `::[...]` between lvalue and `=`) to
      // find an attribute_list.
      std::function<void(TSNode)> capture_attr_list_under = [&](TSNode parent_node) {
        if (it.has_attr_list) {
          return;
        }
        uint32_t total = ts_node_child_count(parent_node);
        for (uint32_t k = 0; k < total; k++) {
          TSNode           kc  = ts_node_child(parent_node, k);
          const char*      kfn = ts_node_field_name_for_child(parent_node, k);
          std::string_view kt(ts_node_type(kc));
          if (kt == "attribute_list") {
            it.attr_list_node = kc;
            it.has_attr_list  = true;
            return;
          }
          // New grammar: the write-side `::[…]` attribute carrier is a
          // `tuple_sq` tagged with the `attribute` field on a `type_cast`.
          if ((kt == "tuple_sq" || kt == "attribute_sq") && kfn && std::string_view(kfn) == "attribute") {
            it.attr_list_node = kc;
            it.has_attr_list  = true;
            return;
          }
          capture_attr_list_under(kc);
          if (it.has_attr_list) {
            return;
          }
        }
      };
      capture_attr_list_under(lv);
      // Some grammar shapes attach the `::[...]` attribute_list to the
      // assignment node directly (between lvalue and rvalue); also scan
      // the assignment's own children but exclude rvalue/operator subtrees.
      if (!it.has_attr_list) {
        uint32_t total = ts_node_child_count(c);
        for (uint32_t k = 0; k < total; k++) {
          TSNode           kc  = ts_node_child(c, k);
          const char*      kfn = ts_node_field_name_for_child(c, k);
          std::string_view kt(ts_node_type(kc));
          if (kt == "attribute_list") {
            it.attr_list_node = kc;
            it.has_attr_list  = true;
            break;
          }
          if ((kt == "tuple_sq" || kt == "attribute_sq") && kfn && std::string_view(kfn) == "attribute") {
            it.attr_list_node = kc;
            it.has_attr_list  = true;
            break;
          }
        }
      }
      if (lvt2 == "attribute_set") {
        // `b::[poison=99]` in expression-lvalue position carries the bare
        // identifier in the `argument` field.
        TSNode arg = child_by_field(lv, "argument");
        if (!ts_node_is_null(arg)) {
          std::string_view at(ts_node_type(arg));
          if (at == "typed_identifier") {
            TSNode id     = child_by_field(arg, "identifier");
            it.assign_key = ts_node_is_null(id) ? trim(get_text(arg)) : trim(get_text(id));
          } else {
            it.assign_key = trim(get_text(arg));
          }
        }
      } else if (lvt2 == "typed_identifier") {
        TSNode id = child_by_field(lv, "identifier");
        if (!ts_node_is_null(id)) {
          it.assign_key = trim(get_text(id));
        }
        // Phase 8: capture the per-field type cast (`:u4`, `:s5`, …) so the
        // post-tuple_add loop can emit a per-field type_spec.
        TSNode tc = child_by_field(lv, "type");
        if (!ts_node_is_null(tc)) {
          it.type_cast_node = tc;
          it.has_type_cast  = true;
        }
      } else {
        // `_complex_identifier` with optional sibling `type` field on the
        // assignment node. Use the lvalue text directly; for plain
        // identifiers it's already clean.
        it.assign_key = trim(get_text(lv));
      }
      if (!it.assign_key.empty()) {
        // A named field that repeats an earlier one in the same bundle literal
        // is a duplicate — reject it (each tuple field must be unique). The
        // span points at this (the second) field so `locate_error_here` lands
        // on the offending line.
        if (!seen_field_keys.insert(it.assign_key).second) {
          report_error(c,
                       "duplicate-tuple-field",
                       "name",
                       std::format("duplicate field `{}` in bundle literal (each field name must be unique)", it.assign_key),
                       "rename or remove the duplicate field");
        }
      } else {
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
      // The field is readable by the LATER fields' initializers (tuple scope)
      // — push after lowering its own value so `a = a + 1` self-reads still
      // resolve against the enclosing scope.
      inflight_name_scopes_.back().push_back(it.assign_key);
      items.push_back(std::move(it));
    } else if (t == "var_or_let_or_reg") {
      // New `_tuple_item` choice: `decl value` (e.g. `(mut 3, const 5)`).
      // `_tuple_item` is hidden so its children show as siblings on the
      // tuple. Pair this kind keyword with the next named sibling, which is
      // either a `typed_identifier` (lvalue declaration form) or an
      // expression (positional value with mutability override). Either way
      // the field is positional — record the value only.
      if (i + 1 < nnc) {
        TSNode           next = kids[i + 1];
        std::string_view nextt(ts_node_type(next));
        Item             it;
        if (nextt == "typed_identifier") {
          // Bare declaration like `mut b` — emit the identifier as the
          // tuple slot's positional value (initial value is undefined).
          TSNode id = child_by_field(next, "identifier");
          if (!ts_node_is_null(id)) {
            it.value      = identifier_to_node(id, true);
            it.assign_key = trim(get_text(id));
          } else {
            it.value = builder.mint_tmp_ref();
          }
          it.declares_field = true;
          // Keep the field's type and `mut` marker: a tuple TYPE that mixes
          // defaulted and undefaulted fields (`(mut flag:Bool = false, mut state:U2)`)
          // turns these into `name = nil` rows below, and `state`'s `U2` would
          // otherwise appear nowhere in the LNAST.
          if (!ts_node_is_null(child_by_field(next, "type"))) {
            it.type_cast_node = child_by_field(next, "type");
            it.has_type_cast  = true;
          }
          it.is_mut = trim(get_text(c)).starts_with("mut");
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
      TSNode id         = child_by_field(c, "identifier");
      it.value          = ts_node_is_null(id) ? builder.mint_tmp_ref() : identifier_to_node(id, true);
      it.declares_field = true;
      items.push_back(std::move(it));
    } else if (t == "lambda" && !ts_node_is_null(child_by_field(c, "name"))) {
      // In-tuple method definition (`comb call(ref self, a) -> (r) { … }`).
      // Hoist the lambda to a top-level func_def under a file-unique name and
      // bind it as a named field holding the function ref — the exact shape
      // of the decorator pattern (`call = docall`), so constprop records the
      // qualified function name and the runner's method dispatch resolves it
      // through the receiver's bundle.
      TSNode      name_node = child_by_field(c, "name");
      std::string mname(trim(get_text(name_node)));
      std::string uniq = std::format("{}__t{}", mname, ++hoisted_lambda_count_);
      process_lambda_statement_named(c, uniq);
      Item it;
      it.is_assign  = true;
      it.assign_key = mname;
      it.value      = Lnast_node::create_ref(uniq);
      if (!seen_field_keys.insert(it.assign_key).second) {
        report_error(c,
                     "duplicate-tuple-field",
                     "name",
                     std::format("duplicate field `{}` in bundle literal (each field name must be unique)", it.assign_key),
                     "rename or remove the duplicate method");
      }
      // Method fields are readable by later field initializers (tuple scope).
      inflight_name_scopes_.back().push_back(it.assign_key);
      items.push_back(std::move(it));
    } else {
      if (t == "typed_field") {
        // Type-tuple field intro (`type Complex = (v1:string, …)`): the
        // field name DECLARES the field — it is not a read of `v1`, and it
        // is readable by the later fields. Register it before lowering so
        // identifier_to_node resolves it in-flight. Canonicalize the escaped
        // form (`` `in` `` → `in`) so the registered name matches the read's
        // canonical name (identifier_to_node), else a backtick-escaped field
        // name is flagged as an undefined read of itself.
        TSNode id = child_by_field(c, "identifier");
        if (!ts_node_is_null(id) && std::string_view(ts_node_type(id)) == "identifier") {
          inflight_name_scopes_.back().emplace_back(canonical_escaped_ident(trim(get_text(id))));
        }
      }
      Item it;
      if (t == "typed_field" && trim(get_text(child_by_field(c, "identifier"))) == "_") {
        // An anonymous tuple-type entry `_:T`: a POSITIONAL slot of the shape seed
        // (its type is stamped on `target.<position>` by emit_tuple_type_field_specs).
        it.value = Lnast_node::create_const("nil");
      } else if (t == "typed_field" && field_types_on_target) {
        // emit_type_spec already stamped target.field. This shape seed only
        // carries labels: typing the bare field here would re-type an
        // unrelated local with the same name (NewCSR's mtopi instance).
        it.value = identifier_to_node(child_by_field(c, "identifier"), true);
      } else {
        it.value = expr_to_node(c);
      }
      items.push_back(std::move(it));
    }
  }

  // A tuple type that MIXES defaulted fields with bare TYPED declared ones
  // (`(mut flag:Bool = false, mut state:U2)`; an untyped `mut c` stays the
  // positional read of the outer `c`): lower each bare field as an
  // explicit `name = nil` row (a named field with a type and no default,
  // 03-bundle.md `const field2:String = nil`) so it carries its name, type and
  // `mut` marker like its defaulted siblings. A tuple whose fields are ALL bare
  // keeps the positional-slot lowering.
  {
    bool any_valued = false;
    for (const auto& it : items) {
      any_valued = any_valued || (it.is_assign && !Lnast::is_tmp(it.assign_key));
    }
    if (any_valued) {
      for (auto& it : items) {
        if (it.declares_field && !it.is_assign && it.has_type_cast && !it.assign_key.empty()) {
          it.is_assign = true;
          it.value     = Lnast_node::create_const("nil");
        }
      }
    }
  }

  // A tuple is ALL-NAMED or ALL-UNNAMED (03-bundle.md: `mut c = (1, const d=4)`
  // is an error). Only a literal bound to an UNTYPED declaration is checked here:
  // a typed destination (`mut x:T = (a="hello", 3)`) may drop names under the
  // call naming rules, and a splice's named/unnamed mix is checked where the
  // spliced tuples are known (constprop's tuple_concat).
  {
    bool any_named = false;
    bool any_pos   = false;
    for (const auto& it : items) {
      if (it.is_spread) {
        continue;
      }
      if ((it.is_assign && !Lnast::is_tmp(it.assign_key)) || it.declares_field) {
        any_named = true;
      } else {
        any_pos = true;
      }
    }
    if (any_named && any_pos) {
      const TSNode parent = ts_node_parent(n);
      if (!ts_node_is_null(parent) && std::string_view(ts_node_type(parent)) == "assignment"
          && !ts_node_is_null(child_by_field(parent, "decl"))) {
        const TSNode lv = child_by_field(parent, "lvalue");
        if (!ts_node_is_null(lv) && std::string_view(ts_node_type(lv)) != "typed_identifier") {
          report_error(n,
                       "tuple-mixed-named",
                       "type",
                       "a tuple cannot mix named and unnamed entries: it must be all-named or all-unnamed",
                       "name every entry (`(a=1, d=4)`) or none of them (`(1, 4)`)");
        }
      }
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
        auto aidx = lnast->add_child(chunk_idx, Lnast_ntype::create_store());
        lnast->add_child(aidx, Lnast_node::create_ref(it.assign_key));
        lnast->add_child(aidx, it.value);
      } else {
        lnast->add_child(chunk_idx, it.value);
      }
    }
    return chunk_ref;
  };

  // Per-field attribute overrides (`b::[poison=99]=2`), per-field type
  // annotations (`a:u4=3`), and `mut` markers — emit a tuple_get tmp bound to
  // the field, then attach decorations against that tmp (the attribute pass
  // alias-chases them onto `tuple_ref.field`). Shared by the no-spread and
  // spread paths so a decorated field in a spread literal (`(...xs, a:u4=3)`)
  // keeps its type/attr/mut instead of silently dropping them.
  auto decorate_fields = [&](const Lnast_node& tuple_ref) {
    for (const auto& it : items) {
      if ((!it.has_attr_list && !it.has_type_cast && !it.is_mut) || !it.is_assign) {
        continue;
      }
      auto tg_idx = builder.add_child(Lnast_ntype::create_tuple_get());
      auto tg_ref = builder.mint_tmp_ref();
      lnast->add_child(tg_idx, tg_ref);
      lnast->add_child(tg_idx, tuple_ref);
      lnast->add_child(tg_idx, Lnast_node::create_const(it.assign_key));
      if (it.is_mut) {
        auto d_idx = builder.add_child(Lnast_ntype::create_declare());
        lnast->add_child(d_idx, tg_ref);
        lnast->add_child(d_idx, Lnast_ntype::create_prim_type_none());
        lnast->add_child(d_idx, Lnast_node::create_const("mut"));
      }
      if (it.has_type_cast) {
        TSNode ty = child_by_field(it.type_cast_node, "type");
        if (!ts_node_is_null(ty)) {
          std::string_view tt(ts_node_type(ty));
          if (tt == "uint_type" || tt == "sint_type" || tt == "bool_type" || tt == "string_type" || tt == "clock_type"
              || tt == "reset_type") {
            auto ts_idx = builder.add_child(Lnast_ntype::create_type_spec());
            lnast->add_child(ts_idx, tg_ref);
            emit_type_expr(ts_idx, ty);
          } else if (tt == "expression_type" || tt == "dot_expression_type" || tt == "function_call_type") {
            check_type_name_spelling(ty);
            auto raw = trim(get_text(ty));
            if (!raw.empty()) {
              auto as_idx = builder.add_child(Lnast_ntype::create_attr_set());
              lnast->add_child(as_idx, tg_ref);
              lnast->add_child(as_idx, Lnast_node::create_const("typename"));
              std::string quoted;
              quoted.reserve(raw.size() + 2);
              quoted.push_back('\'');
              quoted.append(raw);
              quoted.push_back('\'');
              lnast->add_child(as_idx, Lnast_node::create_const(quoted));
            }
          }
        }
      }
      if (it.has_attr_list) {
        emit_attribute_list(tg_ref, it.attr_list_node);
      }
    }
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
        auto aidx = lnast->add_child(idx, Lnast_ntype::create_store());
        lnast->add_child(aidx, Lnast_node::create_ref(it.assign_key));
        lnast->add_child(aidx, it.value);
      } else {
        lnast->add_child(idx, it.value);
      }
    }
    decorate_fields(ref);
    return ref;
  }

  // Spread path: build a list of bundle refs to concat. Each contiguous run
  // of non-spread items becomes a tuple_add tmp; each spread contributes its
  // pre-evaluated inner ref directly. Concat is then a single tuple_concat
  // over those refs.
  std::vector<Lnast_node> chunks;
  std::vector<Item>       pending;
  for (const auto& it : items) {  // copy (not move): items stays intact for decorate_fields below
    if (it.is_spread) {
      if (!pending.empty()) {
        chunks.push_back(emit_chunk_tuple_add(pending));
        pending.clear();
      }
      chunks.push_back(it.value);
    } else {
      pending.push_back(it);
    }
  }
  if (!pending.empty()) {
    chunks.push_back(emit_chunk_tuple_add(pending));
  }

  auto idx = builder.add_child(Lnast_ntype::create_tuple_concat());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  attach_loc(idx, n);  // span → constprop's `...` overlap diagnostic points here
  for (const auto& c : chunks) {
    lnast->add_child(idx, c);
  }
  // Decorate the non-spread fields against the concat result so a decorated
  // field in a spread literal (`(...xs, a:u4=3)`) keeps its type/attr/mut.
  decorate_fields(ref);
  return ref;
}

// ---------------- Fast import scan (lexer-only; see prp2lnast.hpp) ----------------
// Runs ONLY the prpparse lexer — no recursive-descent parse, no LNAST build, no
// elaboration — so it is linear in file size (ms even on multi-MB sources). The
// lexer drops comments/trivia and lexes a string body into one `string` token, so
// an `import` inside a comment or string is never a keyword token and is ignored.
namespace {

std::vector<std::string> scan_import_tokens(prpparse::Source_buffer buf) {
  using namespace prpparse;
  std::vector<std::string> out;

  std::vector<Token> toks = Lexer(buf).tokenize();  // throws Parse_error on an unterminated string

  auto strip = [](std::string_view t) -> std::string {
    if (t.size() >= 2 && (t.front() == '"' || t.front() == '\'') && t.back() == t.front()) {
      return std::string(t.substr(1, t.size() - 2));
    }
    return std::string(t);
  };

  for (size_t i = 0; i < toks.size(); ++i) {
    if (!toks[i].is_kw(Keyword::kw_import)) {
      continue;
    }
    size_t j = i + 1;
    if (j < toks.size() && toks[j].is(Token_kind::lparen)) {
      // call form: `… = import("[path/]file[.member…]")` — dep is the literal's body.
      ++j;
      if (j < toks.size() && (toks[j].is(Token_kind::string) || toks[j].is(Token_kind::istring))) {
        auto s = strip(toks[j].text);
        if (!s.empty()) {
          out.push_back(std::move(s));
        }
      }
    } else if (j < toks.size() && (toks[j].is(Token_kind::string) || toks[j].is(Token_kind::istring))) {
      // statement form with a string module: `import "path/file" as alias` —
      // the only module spelling the parser accepts for this form. Missing it
      // would drop a dependency edge from the incremental closure and let a
      // stale Tier-B generation restore over an edited exporter.
      auto s = strip(toks[j].text);
      if (!s.empty()) {
        out.push_back(std::move(s));
      }
    } else {
      // statement form: `import file[.member…] as alias` — dep is the dotted module path.
      // (`as` is an ident-kw, so test it before the generic ident branch.)
      std::string mod;
      for (; j < toks.size(); ++j) {
        if (toks[j].is_kw(Keyword::kw_as)) {
          break;
        }
        if (toks[j].is(Token_kind::ident) || toks[j].is(Token_kind::dot)) {
          mod += std::string(toks[j].text);
        } else {
          break;
        }
      }
      if (!mod.empty()) {
        out.push_back(std::move(mod));
      }
    }
  }

  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

}  // namespace

std::vector<std::string> Prp2lnast::scan_imports(const std::string& path) {
  return scan_import_tokens(prpparse::Source_buffer::from_file(path));
}

std::vector<std::string> Prp2lnast::scan_imports(std::string_view path, std::string_view source) {
  return scan_import_tokens(prpparse::Source_buffer(std::string(path), std::string(source)));
}

// ---------------- Factory hook ----------------
