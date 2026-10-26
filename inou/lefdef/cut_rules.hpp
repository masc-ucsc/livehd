// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "physical.hpp"
namespace livehd::physical {
// LEF58 property text as whitespace-separated words; ';' is always its own word.
std::vector<std::string> lef58_tokens(const std::string& text);
// Exact nonnegative LEF dimension in DBU within the signed 32-bit range; throws otherwise.
int64_t     exact_units(double um, int64_t dbu);
// Cuts that touch, overlap, or lie closer than spacing on both axes (a
// conservative square keepout around each cut).
inline bool violates_cut_spacing(const Rect& a, const Rect& b, double spacing) {
  const auto dx = std::max({a.xl - b.xh, b.xl - a.xh, 0.});
  const auto dy = std::max({a.yl - b.yh, b.yl - a.yh, 0.});
  return (dx == 0 && dy == 0) || (dx < spacing && dy < spacing);
}
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
// Largest unqualified cut SPACING as a DBU rule distance (0 without any). Empty,
// with *error set when given, for a negative or non-finite spacing ("invalid cut
// spacing") or a result beyond the signed 32-bit range ("unrepresentable cut spacing").
std::optional<int64_t> unqualified_cut_spacing(const Cut_layer&, int64_t dbu, std::string* error = nullptr);
// Maximum ordinary/default-table edge spacing, rejecting unsupported cut features.
int64_t                             signal_cut_spacing(const Cut_layer&, int64_t dbu);
}  // namespace livehd::physical
