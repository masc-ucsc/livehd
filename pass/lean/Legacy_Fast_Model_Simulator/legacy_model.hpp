// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "certificate_ir.hpp"
#include "legacy_emit_options.hpp"
namespace lean_export {
void emit_legacy_model(const DesignScan&, const CertificateIR&, const LegacyEmitOptions&, std::ostream&);
}  // namespace lean_export
