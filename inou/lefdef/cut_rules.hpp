// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "physical.hpp"
namespace livehd::physical {
// Supported LEF58 subset: cut classes, all-DEFAULT pair tables and ASAP7
// enclosure forms. Unknown syntax fails explicitly. All dimensions are DBU.
std::map<std::string, std::array<int64_t, 2>> cut_classes(const Cut_layer&, int64_t dbu,
                                                          std::set<std::string>* single_cut = nullptr);
std::optional<int64_t> cut_table_spacing(const Cut_layer&, int64_t dbu, const std::map<std::string, std::array<int64_t, 2>>&);
// PDNGEN conservatively includes EOL enclosures regardless of eolWidth.
// Signal routing retains the LEF applicability threshold.
enum class Cut_enclosure_policy { lef, pdngen };
std::vector<std::array<int64_t, 2>> cut_enclosures(const Cut_layer&, int64_t dbu,
                                                   const std::map<std::string, std::array<int64_t, 2>>&,
                                                   const std::array<int64_t, 2>& cut_size, bool above, const Layer&,
                                                   const std::array<int64_t, 2>& metal_extent,
                                                   Cut_enclosure_policy policy = Cut_enclosure_policy::lef);
// Maximum ordinary/default-table edge spacing, rejecting unsupported cut features.
int64_t                             signal_cut_spacing(const Cut_layer&, int64_t dbu);
}  // namespace livehd::physical
