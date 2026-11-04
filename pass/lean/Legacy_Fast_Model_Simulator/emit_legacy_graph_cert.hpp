// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "emit_legacy_schema.hpp"
namespace lean_export {
std::string legacy_node_cert(const CertNode&);
void        emit_legacy_graph_cert(const DesignScan&, const CertificateIR&, const LegacyNames&, bool bridge, std::ostream&);
// Above the lookup-tree threshold the certificate part writes its sources and nodes as `OpBridge.BT` trees.
bool        legacy_cert_large(const CertificateIR&);
}  // namespace lean_export
