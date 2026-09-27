// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "certificate_ir.hpp"
namespace lean_export {
void emit_design_cert(const std::string& base, const CertificateIR& certificate, std::ostream& os);
void emit_design_cert(const DesignScan& design, const CertificateIR& certificate, std::ostream& os);
void write_design_cert(const std::string& base, const CertificateIR& certificate, const std::string& path);
}  // namespace lean_export
