// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "emit_legacy_schema.hpp"
namespace lean_export {
std::string legacy_node_cert(const CertNode&);
void        emit_legacy_graph_cert(const DesignScan&, const CertificateIR&, const LegacyNames&, bool bridge, std::ostream&);
}  // namespace lean_export
