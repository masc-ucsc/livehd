//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

namespace upass {

// Shared scalar-kind lattice (1b/D) — one vocabulary for typecheck, the
// Bundle/Entry typed fields, and every pass reading pushed
// operand bundles. `unknown` is the wildcard (skips checks, never errors)
// — also the kind of a typeless single-unknown-bit literal `0sb?`/`0ub?`.
// `nil` is poison. Kind is NOT derivable from the value (`true` reads as
// `1`; `0sb?` is the wildcard), so it must be carried explicitly.
enum class Kind : uint8_t { unknown, integer, boolean, string, range, tuple, nil, enumv, type_name };

// Binding mode — a property of the NAME/binding, not of each leaf (a
// Bundle-level field, never per-Entry). Mirrors the attributes pass's
// Decl_kind vocabulary so the eventual migration is a typedef swap.
//
// `wire_kind` is a single-driver combinational net (2c-wire): like `mut` it
// builds its value from if/match, but its reads are POSITION-INDEPENDENT (every
// read sees the one resolved driver, even before it appears textually — the
// Verilog continuous-assign / net model). Treated like `reg_kind` in the runner
// (never SSA-versioned, store not symbolically bound → reads stay runtime); tolg
// resolves it as a buffered net (no flop) and enforces the single-driver rules.
enum class Mode : uint8_t { unknown, mut_kind, const_kind, reg_kind, await_kind, type_kind, wire_kind };

}  // namespace upass
