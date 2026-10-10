// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "emit_legacy_schema.hpp"
#include "legacy_model.hpp"
namespace lean_export {
void emit_legacy_cert_wf(const CertificateIR&, const LegacyNames&, const LegacyEmitOptions&, std::ostream&);
}
