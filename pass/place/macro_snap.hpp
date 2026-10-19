// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace livehd::placement {
// Strip synthesis-only sub_<number> containers while retaining original paths.
std::string      original_hierarchy_path(const std::string& name);
// A single-memory wrapper inherits its enclosing branching macro scope. Classes
// are scope x physical dimensions, never synthesis color or a type shared across scopes.
std::vector<int> hierarchy_macro_classes(const std::vector<std::string>& names, const std::vector<std::pair<double, double>>& sizes,
                                         const std::vector<std::uint8_t>& macros, int movable);
}  // namespace livehd::placement
