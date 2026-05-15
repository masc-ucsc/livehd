// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// LiveHD-side per-node / per-pin attribute tags backed by HHDS flat_storage.
// Used by Phase G3+ of the HHDS graph migration to host the payload fields
// that LiveHD's Lgraph_attributes currently keeps in dense per-index arrays
// (Ann_node_pin_bits, Ann_node_pin_name, …). See
// docs/contracts/hhds_graph_migration_plan.md.

#include <cstdint>
#include <string>

#include "graph_sizing.hpp"  // Bits_t, Port_ID
#include "hhds/attr.hpp"

namespace livehd::attrs {

// Pin bitwidth — the LiveHD-side `Bits_t` payload that Ann_node_pin_bits
// currently stores. Attached to a hhds::Pin_class via Pin_class::attr().
struct bits_t {
  using value_type = Bits_t;
  using storage    = hhds::flat_storage;
};
inline constexpr bits_t bits{};

// Pin name override. HHDS provides hhds::attrs::name on a per-node basis;
// LiveHD also needs per-pin names (e.g. `set_name` on a driver pin). When
// migrating, a per-pin name should be attached via this tag rather than
// reusing the node-level name attribute.
struct pin_name_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr pin_name_t pin_name{};

// "Is this pin signed?" — currently Ann_node_pin_sign. The value space is a
// small enum (unsigned / signed / unknown); flat_storage<int8_t> is enough.
struct sign_t {
  using value_type = int8_t;
  using storage    = hhds::flat_storage;
};
inline constexpr sign_t sign{};

// LiveHD instance Port_ID for a pin. HHDS uses Port_id for declared-IO pin
// ordering, but LiveHD needs to preserve its own instance_pid across reload
// for sub-graph pin remapping. Attached per-pin.
struct instance_pid_t {
  using value_type = Port_ID;
  using storage    = hhds::flat_storage;
};
inline constexpr instance_pid_t instance_pid{};

// Cell-color taint (Lgraph_attributes::Ann_node_color) — small int.
struct color_t {
  using value_type = int32_t;
  using storage    = hhds::flat_storage;
};
inline constexpr color_t color{};

// Sub-graph reference id — per-node attribute set by Lgraph::set_type_sub
// and read back by Lgraph::get_type_sub. Stores the raw Lg_type_id value
// (uint32_t) rather than the typed alias to keep the attribute storage
// header-light.
struct subid_t {
  using value_type = uint32_t;  // matches Lg_type_id::value
  using storage    = hhds::flat_storage;
};
inline constexpr subid_t subid{};

// Per-node serialized Const value used by Nconst cells. Const::serialize()
// returns std::string; we mirror that as-is. Stored per-node (not per-pin)
// because each Nconst node has exactly one driver carrying the constant.
struct const_value_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr const_value_t const_value{};

// Per-node serialized LUT-table Const value used by LUT cells. Same shape
// as const_value, separate tag so a cell can never be both Nconst and LUT
// in attribute storage.
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
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::pin_name_t>() {
  return "livehd::attrs::pin_name";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::sign_t>() {
  return "livehd::attrs::sign";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::instance_pid_t>() {
  return "livehd::attrs::instance_pid";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::color_t>() {
  return "livehd::attrs::color";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::subid_t>() {
  return "livehd::attrs::subid";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::const_value_t>() {
  return "livehd::attrs::const_value";
}
template <>
[[nodiscard]] inline std::string attr_tag_name<livehd::attrs::lut_t>() {
  return "livehd::attrs::lut";
}

}  // namespace hhds
