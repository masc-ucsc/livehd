// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "emit_legacy_schema.hpp"
namespace lean_export {
bool needs_nary_bridge(const CertificateIR& certificate);
void emit_legacy_fast_bridge(const DesignScan&, const CertificateIR&, const LegacyNames&, std::ostream&);
}  // namespace lean_export
