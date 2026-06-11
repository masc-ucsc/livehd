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
#include "hhds/attr.hpp"

namespace livehd::attrs {

// Per-pin bitwidth (driver pin), plain int32; storage uses uint32_t
// for the flat_storage value to avoid signed-int hashmap key issues.
struct bits_t {
  using value_type = int32_t;
  using storage    = hhds::flat_storage;
};
inline constexpr bits_t bits{};

// Per-pin offset (used by Get_mask / Set_mask / Sext-style ops).
// Replaces Lgraph_attributes::node_pin_offset_map.
struct pin_offset_t {
  using value_type = int32_t;
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

// Per-pin "is signed" marker (present = signed, absent = unsigned).
// Stored as a marker value because HHDS attributes currently require a
// value_type; callers should use graph_util::set_sign/set_unsign instead of
// setting this attr directly.
struct pin_signed_t {
  struct value_type {
    uint8_t marker = 1;
  };
  using storage = hhds::flat_storage;
};
inline constexpr pin_signed_t pin_signed{};

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

// Per-node source provenance is hhds::attrs::srcid (one uint64 SourceId
// resolved through the graph's Source_locator) — the old livehd::attrs::loc
// (with its pos1=line-vs-byte mismatch) and livehd::attrs::source string pair
// were write-only and are gone ([[1f]]).

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

// Task 1u-C — per-pin/node TIME interval annotation (cycles from the
// enclosing partition's inputs; the LG half of the pipe/mod time machinery).
// On a Sub node: the instance's pinned latency contribution (stage[N] pick
// or the callee's declared interval). Derived values are computed by the
// 1u-D checker.
struct time_range_t {
  struct value_type {
    int64_t min = 0;
    int64_t max = 0;
  };
  using storage = hhds::flat_storage;
};
inline constexpr time_range_t time_range{};

// Task 1u-C — PENDING time check: an asserted interval (from an LNAST
// timecheck the discharge pass could not decide, or a mod output's declared
// landing cycle) attached to the checked value's pin. The 1u-D checker
// verifies computed ⊆ asserted and REMOVES the record (`.del()`); records
// still present after the checker are a compile error.
struct pending_time_t {
  struct value_type {
    int64_t min = 0;
    int64_t max = 0;
  };
  using storage = hhds::flat_storage;
};
inline constexpr pending_time_t pending_time{};

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
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::pin_signed_t>() {
  return "livehd::attrs::pin_signed";
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
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::time_range_t>() {
  return "livehd::attrs::time_range";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::pending_time_t>() {
  return "livehd::attrs::pending_time";
}

}  // namespace hhds
