// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <ostream>
#include <string>
#include <string_view>

#include "certificate_ir.hpp"

namespace lean_export {

// DCERT2: the binary-ish wire format the Lean codec `Compiler.CertIO` is PROVED
// to invert (`parseCert_renderCert`, unconditional over every `DesignCert`).
//
// Why this exists next to `emit_design_cert`.  That emitter prints a Lean
// SOURCE literal, which has to be elaborated: a real design is one array
// literal with thousands of elements, and the elaborator is where the
// constructor-arity limit and the multi-GB typecheck costs live.  DCERT2 is
// read at RUN time by a total, verified reader instead, so the same design
// costs a parse.  The two emitters walk the SAME `CertificateIR` and must agree
// field for field; `dcert_parity_test` is where that is checked.
//
// WHAT THIS DOES NOT DO.  It judges nothing.  Clock ordinals are written as
// they stand, slots are written as `index_certificate` assigned them, and a
// certificate that is semantically ill-formed serialises fine -- admission is
// `Compiler.CertIO.certWfError` / `readCertChecked` on the Lean side, exactly
// as for `renderCert`.  Keeping the serialiser opinion-free is what lets the
// round-trip theorem be stated for every `DesignCert`.

// Decimal canonicalisation applied to the IR's integer STRINGS
// (`Source::const_int`, `Source::reset_value`, `FlopDriver::reset_value`,
// `Op::value`).  Lean's `natBytes`/`intBytes` render through `Nat.repr`: no
// leading zeros, no `+`, and no `-` on zero.  An IR string of `007` or `-0` is
// the same INTEGER but not the same BYTES, so it is normalised here rather
// than silently producing a certificate that cannot be byte-compared against
// `renderCert`.  Throws `std::invalid_argument` on anything that is not an
// optionally-signed run of decimal digits -- it is never guessed at.
std::string canonical_decimal(std::string_view value);

// The opcode number Lean's `Compiler.CertIO.opCode` assigns.  Separate from
// `format_op`'s constructor name so a mismatch is a compile-time switch, not a
// string.
uint32_t dcert_opcode(const Op& op);

// The opcode PAYLOAD, as a canonical decimal string.  `Op_Sum` and
// `Op_MemWriteBE` carry a `Nat` that rides in the record's `Int` slot; Lean's
// reader takes `Int.toNat` of it, so a negative payload would be read back as
// `0`.  The payload is a `uint32_t` here and can never be negative, which is
// why the round trip survives -- NOT because the slot is canonical in general.
std::string dcert_oparg(const Op& op);

void        render_dcert(const CertificateIR& certificate, std::ostream& os);
std::string render_dcert(const CertificateIR& certificate);
void        write_dcert(const CertificateIR& certificate, const std::string& path);

}  // namespace lean_export
