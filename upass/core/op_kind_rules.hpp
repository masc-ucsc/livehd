//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>
#include <string_view>

#include "kind.hpp"
#include "lnast_ntype.hpp"

// The operator kind rules (optable.md), shared by upass.typecheck and the
// runner's `tick` body walk (a `tick` body is emitted verbatim, so typecheck
// never sees it). One table, so the two never disagree on what is legal.
namespace upass::op_kind {

// `required == Kind::unknown` is the `==`/`!=` rule: the operands share an
// eq_class instead of one required kind.
struct Rule {
  std::string_view sym;
  Kind             required;
  Kind             result;
  std::string_view code;
};

// The uniform operators: every operand is `required`. `<<` (a tuple amount is
// legal) and `concat` (tuple lanes) have rules of their own.
[[nodiscard]] constexpr std::optional<Rule> rule_of(Lnast_ntype::Lnast_ntype_int t) {
  using N = Lnast_ntype;
  switch (t) {
    case N::Lnast_ntype_plus    : return Rule{"+", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_minus   : return Rule{"-", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_mult    : return Rule{"*", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_div     : return Rule{"/", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_mod     : return Rule{"%", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_bit_and : return Rule{"&", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_bit_or  : return Rule{"|", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_bit_xor : return Rule{"^", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_bit_not : return Rule{"~", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_sra     : return Rule{">>", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_red_or  : return Rule{"|", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_red_and : return Rule{"&", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_red_xor : return Rule{"^", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_popcount: return Rule{"#+", Kind::integer, Kind::integer, "type-mismatch-arith"};
    case N::Lnast_ntype_log_and : return Rule{"and", Kind::boolean, Kind::boolean, "type-mismatch-logical"};
    case N::Lnast_ntype_log_or  : return Rule{"or", Kind::boolean, Kind::boolean, "type-mismatch-logical"};
    case N::Lnast_ntype_log_not : return Rule{"not", Kind::boolean, Kind::boolean, "type-mismatch-logical"};
    case N::Lnast_ntype_eq      : return Rule{"==", Kind::unknown, Kind::boolean, "type-mismatch-eq"};
    case N::Lnast_ntype_ne      : return Rule{"!=", Kind::unknown, Kind::boolean, "type-mismatch-eq"};
    case N::Lnast_ntype_lt      : return Rule{"<", Kind::integer, Kind::boolean, "type-mismatch-compare"};
    case N::Lnast_ntype_le      : return Rule{"<=", Kind::integer, Kind::boolean, "type-mismatch-compare"};
    case N::Lnast_ntype_gt      : return Rule{">", Kind::integer, Kind::boolean, "type-mismatch-compare"};
    case N::Lnast_ntype_ge      : return Rule{">=", Kind::integer, Kind::boolean, "type-mismatch-compare"};
    default                     : return std::nullopt;
  }
}

// Equality classes. `bool` and `string` are distinct (no implicit conversion:
// `bool == int` and `string == int` are errors). int/range/tuple share a
// class: a scalar is a 1-element flat tuple and a range compares to its
// flat-tuple expansion, so `1 == (1,)` and `2..=4 == (2,3,4)` are legal.
// unknown/nil: no class (the check is skipped). (Assignment uses exact-kind
// equality, not this coarse class: a var's type cannot change, even int<->tuple.)
[[nodiscard]] constexpr int eq_class(Kind k) {
  switch (k) {
    case Kind::integer:
    case Kind::range  :
    case Kind::tuple  : return 0;
    case Kind::boolean: return 1;
    case Kind::string : return 2;
    default           : return -1;
  }
}

[[nodiscard]] constexpr std::string_view kind_name(Kind k) {
  switch (k) {
    case Kind::integer: return "integer";
    case Kind::boolean: return "boolean";
    case Kind::string : return "string";
    case Kind::range  : return "range";
    case Kind::tuple  : return "tuple";
    case Kind::nil    : return "nil";
    default           : return "unknown";
  }
}

// An operand's kind as a `name:KIND` annotation in a diagnostic: the Pyrope
// type word where one names the kind (`x:Bool`, `s:String`), the prose kind
// otherwise (`<const>:integer`). The lowercase `boolean`/`string` are banned
// type words, so an annotation never spells them.
[[nodiscard]] constexpr std::string_view kind_annot(Kind k) {
  switch (k) {
    case Kind::boolean: return "Bool";
    case Kind::string : return "String";
    default           : return kind_name(k);
  }
}

// The hint of a logical operator given a non-boolean operand.
inline constexpr std::string_view kLogicalOperandHint
    = "logical ops need boolean operands: test an integer with `x != 0`, or use `&`/`|`/`^` for bitwise integers";

}  // namespace upass::op_kind
