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
// A Sext whose sign position is the constant operand width (or the node width,
// if smaller): the fast model writes `bv_sext a`. At any other position it
// writes `bv_sext (BitVec.ofNat amt a)` (the low `amt` bits, sign-extended).
// The bridge proves the first form by `sext_bridge`/`sext_bridge_low` and the
// second by `sext_bridge_any`. Both legacy emitters call this.
bool legacy_sext_natural(const CertificateIR&, const CertNode&);
// A memory's next image, the certificate's `srcMemNext`, as one Lean expression.
// `img` renders an image slot and `held` is the image kept when nothing commits.
// With no edges record (one domain), `img(next_img)`. One domain among several:
// `img(next_img)` when its edge fires. A commit table (DCERT3, decision #9):
// `img(next_img)` when ALL the memory's domains fire, the entry naming EXACTLY
// the domains that fire, and `held` otherwise. `held` is the stored image, or
// with an asynchronous whole-array reset (`async_img`) that image with the
// reset applied. Both legacy emitters call this.
std::string legacy_memory_next(const MemoryDriver&, const LegacyNames&, const std::function<std::string(uint32_t)>& img,
                               const std::string& held);
void emit_legacy_fast_model(const DesignScan&, const CertificateIR&, const LegacyNames&, bool bridge, std::ostream&);
}  // namespace lean_export
