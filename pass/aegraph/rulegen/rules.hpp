//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The rule DSL (todo/livehd/2a-aegraph.html, "Rule API"): the syntax tree of a
// .rules file, its parser, the lints that fail generation, and the emitters
// (rule C++, the per-group fuzz and prove tests, RULES.md).
//
//   (simplify NAME [PRIO] LHS [:let (v EXPR)...] [:when GUARD...] => RHS [OPTS])
//   (alt      NAME [PRIO] LHS [:let ...] [:when ...] => RHS [OPTS])
//   (choice   NAME [PRIO] LHS [:let ...] [:when ...] => RHS1 [?q := C1] | RHS2 [?q := C2] [OPTS])
//   OPTS := :example TERM | :expect (PROFILE lhs|rhs)... | :fuel N | :proof bv|int|lean|none | :doc "..."
//
// Patterns and terms share one tree (`Pat`):
//
//   x            binds a class (a repeated x is the same class)
//   _            wildcard
//   0 -1         a constant of that value
//   (k c)        binds a fully known constant (as a class and as a value)
//   ?q           binds a `?` literal leaf
//   (Op p...)    positional operands, exact arity; a banked op (Sum Mult And Or Xor
//                Ror EQ LT GT) takes them as an unordered bank (LT/GT: left, right)
//   (Op lo: p..) operands by pin name
//   (Op (as p.. ..r) (bs ..))  bank multisets; ..r binds the rest; a bank the
//                pattern does not write must be empty
//   Op{attr}     a contract attribute on a right-hand-side constructor

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cell.hpp"

namespace livehd::aegraph::rulegen {

using Diags = std::vector<std::string>;  // "file:line: error: message"

// A `:let` / `:when` / `(k EXPR)` expression: numbers, names and builtin calls.
struct Expr {
  enum class Kind : uint8_t { num, var, call };
  Kind              kind = Kind::num;
  int64_t           num  = 0;
  std::string       name;  // var name, or the call head
  std::vector<Expr> args;
  int               line = 0;
};

struct Pat;

struct Bank {
  uint32_t         id = 0;  // 0 = as, 1 = bs
  std::vector<Pat> elems;
  bool             has_rest = false;
  std::string      rest;  // the ..name binder
  int              line = 0;
};

struct Pat {
  enum class Kind : uint8_t { wild, var, num, konst, lit, op };

  Kind                        kind = Kind::wild;
  std::string                 name;  // var, konst binder ("" / "_" = unnamed) or lit (with its '?')
  int64_t                     num = 0;
  std::shared_ptr<const Expr> expr;  // term side: (k EXPR)
  Ntype_op                    op = Ntype_op::Invalid;
  std::string                 op_text;
  std::vector<std::string>    attrs;
  bool                        banked = false;
  std::vector<uint32_t>       pids;   // fixed-pin form: the pid of each operand
  std::vector<Pat>            args;   // fixed-pin form: the operands
  std::vector<Bank>           banks;  // banked form
  int                         line = 0;
};

struct Choice_arm {
  Pat                         rhs;
  std::string                 lit;       // the ?q the arm assigns
  bool                        nonzero = false;
  std::optional<int64_t>      value;     // ?q := C
  bool                        has_assign = false;
  std::string                 assign_text;  // an unsupported (expression) assignment, for the diagnostic
  int                         line = 0;
};

struct Expect {
  std::string profile;
  std::string side;
  int         line = 0;
};

struct Rule {
  enum class Tag : uint8_t { simplify, alt, choice };

  std::string                          name;
  Tag                                  tag  = Tag::simplify;
  int                                  prio = 0;
  Pat                                  lhs;
  std::vector<std::pair<std::string, Expr>> lets;
  std::vector<Expr>                    whens;
  std::vector<Choice_arm>              arms;  // simplify / alt: exactly one, without assignments
  std::optional<Pat>                   example;
  std::vector<Expect>                  expects;
  std::optional<int64_t>               fuel;
  std::string                          proof = "bv";
  std::string                          doc;
  int                                  line = 0;
};

struct Rules_file {
  std::string       group;
  std::string       path;  // as given to rulegen: the #line target
  std::string       text;  // the source, embedded verbatim in the generated tests
  std::vector<Rule> rules;
};

// analyses.decl
struct Analysis {
  std::string              name;
  bool                     is_pred = false;  // (pred ...) vs (analysis ...)
  std::vector<std::string> params;
  std::string              uses;
  std::string              type;
  std::optional<Expr>      spec;
  int                      line = 0;
};

struct Analyses {
  std::string           path;
  std::string           text;
  std::vector<Analysis> list;
  [[nodiscard]] const Analysis* find(std::string_view name) const;
};

// A hand-written rule found in a rules/*.cpp file (AEG_NATIVE_RULE / AEG_RULE).
struct Native_rule {
  std::string              name;
  std::string              tag;
  std::string              group;  // the file's AEG_RULE_GROUP
  std::string              file;   // base name
  int                      line = 0;
  int                      prio = 0;
  std::vector<std::string> roots;
  std::string              doc;  // the comment right above it
  bool                     native = true;
};

[[nodiscard]] Rules_file parse_rules(std::string_view text, std::string_view path, std::string_view group, Diags& diags);
[[nodiscard]] Analyses   parse_analyses(std::string_view text, std::string_view path, Diags& diags);
[[nodiscard]] std::vector<Native_rule> scan_native(std::string_view cpp_text, std::string_view path);

// The lints that fail generation (all diagnostics, not just the first).
void lint(const std::vector<Rules_file>& files, const Analyses& analyses, const std::vector<Native_rule>& natives, Diags& diags);

// ---- shared helpers -------------------------------------------------------------

struct Op_shape {
  bool                     banked     = false;
  int                      bank_count = 0;
  bool                     unlimited  = false;  // Mux, Hotmux, Concat: operand i is pid i
  std::vector<uint32_t>    pids;                // fixed-pin ops: the pid of each pin, ascending
  std::vector<std::string> names;               // and their names
};
[[nodiscard]] Op_shape shape_of(Ntype_op op);
// The ops a rule may name (the pure e-graph ops).
[[nodiscard]] bool is_rule_op(Ntype_op op);
// "Sum" for Ntype_op::Sum, the spelling the generated code and RULES.md use.
[[nodiscard]] std::string op_spelling(Ntype_op op);
[[nodiscard]] std::string expr_text(const Expr& e);
// Canonical text of a pattern / term / rule.
[[nodiscard]] std::string pat_text(const Pat& p);
[[nodiscard]] std::string rule_text(const Rule& r);
// pat_text with the variables renamed by first appearance (overlap detection).
[[nodiscard]] std::string canonical_text(const Pat& p);

// How many e-nodes a pattern / term spells (ops only).
[[nodiscard]] int op_count(const Pat& p);

// ---- emitters -------------------------------------------------------------------

[[nodiscard]] std::string emit_rules_cpp(const Rules_file& file, const Analyses& analyses);
[[nodiscard]] std::string emit_fuzz_test(const Rules_file& file, const Analyses& analyses);
[[nodiscard]] std::string emit_prove_test(const Rules_file& file, const Analyses& analyses);
[[nodiscard]] std::string emit_rules_md(const std::vector<Rules_file>& files, const Analyses& analyses,
                                        const std::vector<Native_rule>& natives);

// The C++ spelling of a runtime-implemented analysis predicate, with `%0`, `%1`... for the
// argument classes; null when the analysis has no runtime implementation yet.
[[nodiscard]] const char* analysis_cxx(std::string_view name);

// The proof-tier label of a rule: "bv-swept" for a pattern rule, "native-unverified" otherwise.
[[nodiscard]] std::string proof_tier(const Rule& r);

}  // namespace livehd::aegraph::rulegen
