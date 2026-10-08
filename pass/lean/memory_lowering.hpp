// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
namespace lean_export {
struct Memory;
namespace detail {
struct CertificateBuilder;
void lower_memory(CertificateBuilder& builder, const Memory& memory);
}  // namespace detail
}  // namespace lean_export
