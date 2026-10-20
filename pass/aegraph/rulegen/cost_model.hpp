//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The cost profiles of the aegraph (todo/livehd/2a-aegraph.html, milestone I): pass/aegraph/cost/<profile>.cost.
//
//   (profile NAME [:default-width N] [:doc "..."])
//   (cost NAME PATTERN [:when GUARD...] PRICE [:doc "..."])
//
//   PATTERN := OP | _ | (OP item...)           first matching entry wins, file order; the last entry is `_`
//   item    := _ | (k) | (k name) | ..         `_` any operand, (k) a fully known int64 constant operand
//                                              (bound to `name` when given), `..` any further operands
//
// A positional op (Mux, SHL, Get_mask, ...) matches the items against its operands in pin order; a banked op
// (Sum Mult And Or Xor Ror EQ) matches them as an unordered multiset. A pattern without items matches any
// operands. PRICE and GUARD use the constant-expression builtins of the rule DSL (rulegen/rule_expr.hpp) over
//
//   W      the result width in bits          A      the operand count
//   Wmax   the widest operand                Wsum   the sum of the operand widths
//   N      the 64-bit words of the result     Nmax   the 64-bit words of the widest operand (both at least 1)
//   name   a constant bound by `(k name)`
//
// Widths are the driver stamp when there is one, else the range analysis, else the profile's :default-width.
// A guard that does not hold, or an expression with no value (an overflow, log2 of 0), makes the entry not match.
// A price is clamped to [0, kMaxPrice]. This file is the ONE implementation: the engine's extractor, rulegen's
// per-profile cost lint and the generated :expect tests all call `price`.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cell.hpp"
#include "rule_expr.hpp"

namespace livehd::aegraph::cost {

inline constexpr int64_t kMaxPrice = int64_t{1} << 20;

// One operand of the node being priced.
struct Operand {
  uint32_t bank     = 0;      // the sink bank / pin id (operands come in ascending order)
  int64_t  width    = 0;      // bits
  bool     is_const = false;  // a fully known constant that fits an int64
  int64_t  value    = 0;      // valid when is_const
};

// A compiled expression.
struct Code {
  enum class Kind : uint8_t { num, W, A, Wmax, Wsum, N, Nmax, bound, call };
  Kind                   kind  = Kind::num;
  int64_t                num   = 0;  // num: the value; bound: the binding index
  const rt::ex::Builtin* fn    = nullptr;
  std::vector<Code>      args;
  int                    line  = 0;
};

struct Item {
  enum class Kind : uint8_t { any, konst, rest };
  Kind        kind = Kind::any;
  std::string name;  // konst: the binder ("" = unnamed)
  int         bind = -1;
};

struct Entry {
  Ntype_op          op = Ntype_op::Invalid;
  bool              any_op = false;      // `_`
  bool              has_items = false;   // `(OP item...)` form
  std::vector<Item> items;
  bool              has_rest = false;
  std::vector<std::string> binds;        // binder names, by index
  std::vector<Code> whens;
  Code              price;
  std::string       doc;
  int               line = 0;
};

struct Profile {
  std::string        name;
  int64_t            default_width = 32;
  std::string        doc;
  std::vector<Entry> entries;
  std::string        path;
  int                line = 0;
};

using Diags = std::vector<std::string>;  // "file:line: error: message"

// Parse one profile file. Diagnostics are appended (errors and warnings, "file:line: error: ...").
[[nodiscard]] Profile parse_profile(std::string_view text, std::string_view path, Diags& diags);

// The checks that need the whole profile: the catch-all last entry, shadowed entries.
void lint_profile(const Profile& profile, Diags& diags);

// The price of one node under `profile`. The catch-all entry makes this total for a linted profile; a profile
// with no matching entry prices 0.
[[nodiscard]] int64_t price(const Profile& profile, Ntype_op op, int64_t width, std::span<const Operand> operands);

// The line of the entry that priced the node (0 = none), for diagnostics.
[[nodiscard]] int priced_by(const Profile& profile, Ntype_op op, int64_t width, std::span<const Operand> operands);

}  // namespace livehd::aegraph::cost
