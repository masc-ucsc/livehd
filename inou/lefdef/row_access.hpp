// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "physical.hpp"

namespace livehd::physical {
// Routing access a placement row offers to the pins inside it.
enum class Row_access : uint8_t { horizontal, vertical };
std::string_view row_access_name(Row_access);
// Odd/even placement-row access convention. The routing owner supplies it
// (read_row_access); this code only consumes it. Default-constructed it is the
// provisional stand-in: the bottom core row is index 0, even rows give
// horizontal access and odd rows vertical access.
struct Row_access_convention {
  // Rows are counted upward from the bottom core row of the floorplan.
  enum class Origin : uint8_t { first_core_row };
  std::string name = "stand-in", version = "1";
  Origin      origin     = Origin::first_core_row;
  uint32_t    index_base = 0;  // index of the bottom core row; only its parity matters
  Row_access  even = Row_access::horizontal, odd = Row_access::vertical;
  // Access of the core row row_offset rows above the bottom core row.
  Row_access  at(size_t row_offset) const { return (row_offset + index_base) % 2 ? odd : even; }
  // Canonical JSON of every field, accepted by read_row_access. Model sidecars
  // and cache keys hash it.
  std::string identity() const;
  friend bool operator==(const Row_access_convention&, const Row_access_convention&) = default;
};
// Strict schema-1 JSON object: "schema": 1, "name", "version", "even", "odd"
// ("horizontal" | "vertical"); optional "origin" ("first_core_row") and
// "index_base". Unknown keys and invalid values throw std::invalid_argument.
Row_access_convention parse_row_access(std::string_view json);
Row_access_convention read_row_access(const std::string& json_path);
// Internal value: default, serialized convention, or a workdir sidecar path.
Row_access_convention resolve_row_access(std::string_view option);
// Core row holding local row `half` of a `rows`-row master whose bottom edge
// sits on core row start_row in orientation o: N/FN keep the local row order,
// FS/S reverse it. Rotated orientations and half >= rows throw.
size_t                placed_row_of_half(size_t start_row, size_t rows, size_t half, Orientation o);
// True when every local row's access (halves[h], bottom-up in the master)
// equals the access of the core row it lands on.
bool row_access_matches(const Row_access_convention&, size_t start_row, Orientation, std::span<const Row_access> halves);
}  // namespace livehd::physical
