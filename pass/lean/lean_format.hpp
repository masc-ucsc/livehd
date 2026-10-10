// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "certificate_ir.hpp"
namespace lean_export {
std::string format_op(const Op& op);
std::string sanitize_lean(std::string_view name);
std::string lean_integer(std::string_view decimal);
// A Lean STRING LITERAL, quoted and escaped. Distinct from `sanitize_lean`,
// which makes an IDENTIFIER: a clock's name is data inside the certificate, not
// a declaration name, so mangling it would lose the net it came from while
// escaping it keeps the name readable and the literal well-formed.
std::string lean_string(std::string_view text);
std::string nat_array(const std::vector<uint32_t>& values);
std::string lit_bv(uint32_t width, std::string_view integer_expression);
std::string lit_zero(uint32_t width);
std::string lit_one(uint32_t width);
std::string nat_list(const std::vector<uint32_t>& values);
std::string bst_literal(std::vector<std::pair<uint32_t, std::string>> pairs);
void        write_atomic(const std::string& path, const std::function<void(std::ostream&)>& emit);
}  // namespace lean_export
