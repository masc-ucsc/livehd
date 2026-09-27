// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>
namespace lean_export {
std::string sanitize_lean(std::string_view name);
std::string lean_integer(std::string_view decimal);
std::string nat_array(const std::vector<uint32_t>& values);
void        write_atomic(const std::string& path, const std::function<void(std::ostream&)>& emit);
}  // namespace lean_export
