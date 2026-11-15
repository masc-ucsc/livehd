//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The LNAST side of the tree aegraph: which statements map to e-nodes (the
// mapping upass.tolg lowers with, so a rewrite means on the tree exactly what it
// would mean on the graph tolg builds), the value kind of a statement (bool vs
// integer: a later inliner re-walk type-checks every statement again, so a
// rewrite must never move a value across kinds), and tolg's shape-matched
// statement classes a rewrite has to preserve.

#include <cstdint>
#include <span>

#include "cell.hpp"
#include "lnast_ntype.hpp"

namespace upass::aeg {

enum class Kind : uint8_t { unknown, boolean, integer };

// The kind two definitions of one name agree on (unknown when they do not).
[[nodiscard]] constexpr Kind meet(Kind a, Kind b) { return a == b ? a : Kind::unknown; }

// Value statements the import table covers (their operand shape is checked at
// import time: a statement of a mapped kind with an unmapped shape is opaque).
[[nodiscard]] bool is_mapped_op(Lnast_ntype::Lnast_ntype_int t);

// The op classes upass.tolg's masked_pw_candidate accepts between the three
// statements of a memory partial write (upass_tolg.cpp, pure_temp_op). A
// rewrite that moved a statement out of this set would hide the triple from it.
[[nodiscard]] bool pure_temp_op(Lnast_ntype::Lnast_ntype_int t);

// The kind of a value statement's result from its op and operand kinds: compares
// and log_not are bool, arithmetic, shifts and slices are integer, and the
// bitwise and logical and/or/xor carry their operands' kind when every operand
// has the same known kind.
[[nodiscard]] Kind result_kind(Lnast_ntype::Lnast_ntype_int t, std::span<const Kind> operand_kinds);

// The root e-node op of a one-node import that the flatten rule chains on
// (Sum, Mult, And, Or, Xor); Ntype_op::Invalid for every other statement.
[[nodiscard]] Ntype_op chain_op(Lnast_ntype::Lnast_ntype_int t);

// Compares: their operands must share a kind (constprop's compare-type-mismatch).
[[nodiscard]] bool is_compare(Lnast_ntype::Lnast_ntype_int t);

}  // namespace upass::aeg
