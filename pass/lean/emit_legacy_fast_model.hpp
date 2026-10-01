// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>

#include "emit_legacy_schema.hpp"
namespace lean_export {
struct FastExpr {
  std::string type;
  // @0@, @1@, ... refer to the node's already-lowered dependency positions.
  std::string text;
};
const Source* certificate_source(const CertificateIR&, uint32_t id);
uint32_t      certificate_width(const CertificateIR&, uint32_t id);
bool          certificate_memory_value(const CertificateIR&, uint32_t id);
FastExpr      lower_fast_expr(const CertificateIR&, const CertNode&);
std::string   render_fast_expr(const FastExpr&, const CertNode&, const std::function<std::string(uint32_t)>&);
std::string   legacy_source_leaf(const CertificateIR&, const LegacyNames&, const Source&);
std::string   legacy_source_value(const CertificateIR&, const LegacyNames&, const Source&);
std::string   legacy_value_wrap(const LegacyNames&, std::string inner, bool memory);
std::string   legacy_fast_ref(const CertificateIR&, const LegacyNames&, uint32_t id, bool bridge);
std::vector<std::optional<uint32_t>> legacy_output_ids(const DesignScan&, const CertificateIR&);
void emit_legacy_fast_model(const DesignScan&, const CertificateIR&, const LegacyNames&, bool bridge, std::ostream&);
}  // namespace lean_export
