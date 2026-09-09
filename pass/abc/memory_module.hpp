// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::abc {
// Replace memories selected for lowering with named child modules. Native
// memories whose cgen form is an inline array are also enclosed in a module.
// The returned definitions belong to the parents' scratch graph library.
std::vector<std::shared_ptr<hhds::Graph>> build_memory_modules(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, bool lower,
                                                               uint64_t max_bits);
}  // namespace livehd::abc
