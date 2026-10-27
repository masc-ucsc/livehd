// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// Same-interface custom cell variants (todo/livehd/2d-domplace.html, G):
// placement-side sizing choices for sequential custom macros that have no
// Liberty entry, such as the provisional domino masters. Data only, so
// pass/place never depends on the generator; lhd fills the catalog from the
// bundle sidecar.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"
#include "pass/liberty/liberty_dff.hpp"

namespace livehd::placement {
// One drive variant. Variants of one family share the function, the input
// pins (rails), the clock and the output, so the same pins connect; they
// differ in drive, capacitances, area and width (so possibly pin offsets).
struct Custom_variant {
  std::string                          name, family, register_cell;
  unsigned                             rank = 0;  // drive order within the family
  std::vector<std::string>             rails;
  std::string                          clock = "CLK", output = "Q";
  double                               area_um2 = 0, estimate_um2 = 0;  // LEF footprint; Liberty-basis estimate
  int64_t                              width_dbu = 0;
  std::vector<std::optional<double>>   rails_ff;  // per rail, in `rails` order; labeled estimates, absent when unknown
  std::optional<double>                clock_ff;
  std::optional<liberty::Output_drive> drive;
};
struct Custom_catalog {
  std::map<std::string, Custom_variant, std::less<>> variants;
  std::map<std::string, std::vector<std::string>>    families;  // variant names by ascending rank
  std::string                                        identity;  // digest of every field, for cache keys
  const Custom_variant*                              find(std::string_view name) const;
  // The variant `delta` ranks away in the same family; nullptr past either end.
  const Custom_variant*                              step(std::string_view name, int delta) const;
};
// Indexes the variants. Names are unique, a family shares rails, clock and
// output, and its ranks are distinct; otherwise std::invalid_argument.
std::shared_ptr<const Custom_catalog> custom_catalog(std::vector<Custom_variant> variants);

// The body-less declaration of `to` in `library`, cloned pin for pin (names,
// port ids, widths, sign, loop breaks) from `like`, the declaration of a
// variant of the same family; an existing equal declaration is reused. A pin
// set other than to's rails, clock and output, a declaration with a body or a
// different one under the same name throws std::invalid_argument.
std::shared_ptr<hhds::GraphIO> declare_custom_cell(hhds::GraphLibrary& library, const hhds::GraphIO& like,
                                                   const Custom_variant& to);
// Swap an instance of `from` to `to` (same family). The new declaration has
// the same port ids, so every pin, edge and pin attribute stays: R*, CLK and
// Q keep their connections, names and roles. A family or interface mismatch,
// or an instance that is not of `from`, throws std::invalid_argument and
// leaves the instance unchanged.
void                           change_custom_master(hhds::Node_class node, const Custom_variant& from, const Custom_variant& to);
// Point an instance of a body-less custom declaration at `name`, declared with
// the same pins (port ids, widths, sign, loop breaks) when absent: a geometry
// template of the same cell, such as a multi-row master's rail twin, which
// differs only in its physical PG rails. Every pin, edge and pin attribute
// stays. A declaration with a body, or another declaration under `name` with
// other pins, throws std::invalid_argument.
void                           change_custom_declaration(hhds::Node_class node, const std::string& name);
}  // namespace livehd::placement
