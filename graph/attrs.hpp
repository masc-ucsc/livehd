// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// LiveHD-side per-node / per-pin attribute tags backed by HHDS flat_storage.
//
// These replace the dense side maps that used to live on
// `Lgraph_attributes` (node_pin_offset_map, node_pin_name_map, etc.). The
// idea (per the HHDS migration plan) is that LiveHD passes carry their
// per-node / per-pin payload as HHDS attributes attached to
// hhds::Node_class / hhds::Pin_class, instead of through a parallel LiveHD
// data structure keyed on Index_id.
//
// Existing built-in HHDS attributes used by LiveHD:
//   - hhds::attrs::name (std::string) → node instance name (replaces
//     Node_name_map).
//
// All tags below are registered at static-init time in graph/cell.cpp's
// init block, mirroring the lnast/lnast.cpp pattern (avoids a registry
// race on first lazy registration).

#include <cstdint>
#include <string>

#include "ann_place.hpp"
#include "graph_sizing.hpp"
#include "hhds/attr.hpp"

namespace livehd::attrs {

// Per-pin bitwidth (driver pin).  Bits_t is int32; storage uses uint32_t
// for the flat_storage value to avoid signed-int hashmap key issues.
struct bits_t {
  using value_type = Bits_t;
  using storage    = hhds::flat_storage;
};
inline constexpr bits_t bits{};

// Per-pin offset (used by Get_mask / Set_mask / Sext-style ops).
// Replaces Lgraph_attributes::node_pin_offset_map.
struct pin_offset_t {
  using value_type = Bits_t;
  using storage    = hhds::flat_storage;
};
inline constexpr pin_offset_t pin_offset{};

// Per-pin wire name override. Replaces Lgraph_attributes::node_pin_name_map.
// The reverse map (Node_pin_name_rmap) is *derived* — if a pass needs
// "find pin by name", it walks all pins or maintains its own side index.
struct pin_name_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr pin_name_t pin_name{};

// Per-pin delay (combinational delay annotation, float seconds).
// Replaces Lgraph_attributes::node_pin_delay_map.
struct pin_delay_t {
  using value_type = float;
  using storage    = hhds::flat_storage;
};
inline constexpr pin_delay_t pin_delay{};

// Per-pin "is unsigned" flag (1 = unsigned, absent = signed).
// Replaces Lgraph_attributes::node_pin_unsigned_map. Stored as int8_t for
// flat_storage's trivially-copyable requirement.
struct pin_unsigned_t {
  using value_type = int8_t;
  using storage    = hhds::flat_storage;
};
inline constexpr pin_unsigned_t pin_unsigned{};

// Per-node color (small int taint used by pass/label and pass diagnostics).
// Replaces Lgraph_attributes::node_color_map.
struct color_t {
  using value_type = int32_t;
  using storage    = hhds::flat_storage;
};
inline constexpr color_t color{};

// Per-node place annotation (ArchFP / physical-design floorplan rectangle).
// Replaces Lgraph_attributes::node_place_map.
struct place_t {
  using value_type = Ann_place;
  using storage    = hhds::flat_storage;
};
inline constexpr place_t place{};

// Per-node source location (pos1 / pos2 offsets into the source file).
// Replaces Lgraph_attributes::node_loc_map.
struct loc_t {
  struct value_type {
    uint64_t pos1 = 0;
    uint64_t pos2 = 0;
  };
  using storage = hhds::flat_storage;
};
inline constexpr loc_t loc{};

// Per-node source filename. Replaces Lgraph_attributes::node_source_map.
struct source_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr source_t source{};

// Per-node serialized Const value used by Nconst cells.
// Replaces Lgraph_attributes::const_map (which stored Const::serialize()).
struct const_value_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr const_value_t const_value{};

// Per-pin serialized Const value carried on CONST_NODE pins whose port_id is
// beyond the small-int fast-path range (`Const_small_pid_count`). For pins in
// the small-int range, the value is encoded directly in the port_id (scheme
// A) and this attribute is absent.
struct pin_const_value_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr pin_const_value_t pin_const_value{};

// Per-node serialized LUT-table Const used by LUT cells.
// Replaces Lgraph_attributes::lut_map.
struct lut_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr lut_t lut{};

}  // namespace livehd::attrs

namespace hhds {

template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::bits_t>() {
  return "livehd::attrs::bits";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::pin_offset_t>() {
  return "livehd::attrs::pin_offset";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::pin_name_t>() {
  return "livehd::attrs::pin_name";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::pin_delay_t>() {
  return "livehd::attrs::pin_delay";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::pin_unsigned_t>() {
  return "livehd::attrs::pin_unsigned";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::color_t>() {
  return "livehd::attrs::color";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::place_t>() {
  return "livehd::attrs::place";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::loc_t>() {
  return "livehd::attrs::loc";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::source_t>() {
  return "livehd::attrs::source";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::const_value_t>() {
  return "livehd::attrs::const_value";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::pin_const_value_t>() {
  return "livehd::attrs::pin_const_value";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::lut_t>() {
  return "livehd::attrs::lut";
}

}  // namespace hhds
