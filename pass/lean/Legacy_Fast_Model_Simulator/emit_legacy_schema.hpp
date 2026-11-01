// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "certificate_ir.hpp"
namespace lean_export {
// Presentation only: semantic identities and runtime ordinals belong to the IR.
struct LegacyNames {
  std::string                                        base;
  std::vector<std::string>                           inputs, outputs;
  std::map<uint32_t, std::string>                    flops, memories;
  std::map<std::pair<uint32_t, size_t>, std::string> read_registers;
  bool                                               sequential    = false;
  bool                                               memory_values = false;
  // One `Bool` field of `<base>_edges` per clock domain, indexed by the
  // domain's ordinal (DesignScan::clocks order), when the design has two or
  // more domains. Empty for one domain: then no edges record exists and every
  // emitted line is what it was before multi-domain support.
  std::vector<std::string>                           edges;
  std::string                                        flop_field(const FlopDriver&) const;
  std::string                                        parameters() const;
  std::string                                        arguments() const;
  // `(e : <base>_edges) ` / `e ` for the next-state functions, or "".
  std::string                                        edge_parameter() const;
  std::string                                        edge_argument() const;
};
LegacyNames legacy_names(const DesignScan&, std::string_view top);
void        emit_legacy_schema(const DesignScan&, const LegacyNames&, std::ostream&);
void        emit_legacy_field_mapping(const DesignScan&, const LegacyNames&, std::ostream&);
}  // namespace lean_export
