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

// Where an attribute legitimately lives — used by the graph_util pin setters to
// assert (and, for `node`, statically reject) the wrong pin role at the call
// site. The per-pin attr key folds the driver/sink bit (hhds Pin_class::attr
// masks Pid 0x2), so a same-port driver and sink SHARE a slot; stamping the
// wrong role then silently aliases the other's value. Classifying each attr lets
// the setters catch that:
//   node        - per node; never set through a pin setter (compile error if so)
//   driver_pin  - per DRIVER pin (the signal source): bits, signed, … (assert is_driver)
//   sink        - per SINK pin (arrival-style)                         (assert is_sink)
//   edge        - shared driver<->sink across the net (the wire name); read on
//                 either end via the shared slot, so no driver/sink restriction
//   any_pin     - any pin, no driver/sink restriction (IO-port offsets, mixed)
enum class Attr_kind { node, driver_pin, edge, sink, any_pin };

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

// Per-hierarchical-instance color (pass/color "hier" storage mode). Unlike
// the flat `color` above (one value per node-in-a-def), this is keyed per
// instance, so two instances of the same module def can carry different ids.
// Readers prefer this when present and fall back to the flat color otherwise
// (graph_util::node_color_of). Written only when pass.color hier_color=1.
struct hier_color_t {
  using value_type = int32_t;
  using storage    = hhds::hier_storage;
};
inline constexpr hier_color_t hier_color{};

// Graph-level active coloring descriptor (serialized JSON, one per top graph)
// produced by pass/color and consumed by pass/partition. Stored on the
// Graph's own Attr_host under a sentinel key (graph_util::coloring_info_key),
// so it persists inside the graph_library with the body. See 2c-color.
struct coloring_info_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr coloring_info_t coloring_info{};

// Per-node / per-pin structural-correspondence id (pass/semdiff, task
// 2f-semdiff). Two corresponding nodes across a ref/impl pair share one id; a
// node with no counterpart gets 0. Stamped on the node AND its driver pin(s)
// with the same id so both node-target and pin-target `tool grep` work.
//
// SPARSE layout (the flat_storage default), *not* dense: a dense store reserves
// value_type{} (== 0) as the "absent" sentinel, but here 0 must be a real,
// greppable value (the unmatched marker `match=0`), so it stays sparse.
struct match_t {
  using value_type = uint32_t;
  using storage    = hhds::flat_storage;
};
inline constexpr match_t match{};

// Per-node formal-verification status (pass/formal, task 2f-formal). SPARSE
// (like match) so 0 is a real value. `proven` marks an obligation (a user
// assert / assume / a built-in Hotmux one-hotness) that pass.formal discharged
// by SMT: cgen elides its runtime check, and pass.abc may exploit a proven
// assume as a don't-care. `runtime_check` marks an obligation deferred to
// runtime (Unknown / budget-exceeded / unreachable stateful refutation) so cgen
// keeps emitting the check. Values are small enums (see graph_util below).
struct proven_t {
  using value_type = uint32_t;
  using storage    = hhds::flat_storage;
};
inline constexpr proven_t proven{};

struct runtime_check_t {
  using value_type = uint32_t;
  using storage    = hhds::flat_storage;
};
inline constexpr runtime_check_t runtime_check{};

// Per-node source provenance is hhds::attrs::srcid (one uint64 SourceId
// resolved through the graph's Source_locator) — the old livehd::attrs::loc
// (with its pos1=line-vs-byte mismatch) and livehd::attrs::source string pair
// were write-only and are gone.

// Per-node serialized Dlop value used by Nconst cells.
// Replaces Lgraph_attributes::const_map (which stored Dlop::serialize()).
struct const_value_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr const_value_t const_value{};

// Per-pin serialized Dlop value carried on CONST_NODE pins whose port_id is
// beyond the small-int fast-path range (`Const_small_pid_count`). For pins in
// the small-int range, the value is encoded directly in the port_id (scheme
// A) and this attribute is absent.
struct pin_const_value_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr pin_const_value_t pin_const_value{};

// Per-node serialized LUT-table Dlop used by LUT cells.
// Replaces Lgraph_attributes::lut_map.
struct lut_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr lut_t lut{};

// Per-pin/node TIME interval annotation (cycles from the
// enclosing partition's inputs; the LG half of the pipe/mod time machinery).
// On a Sub node: the instance's pinned latency contribution (stage[N] pick
// or the callee's declared interval). Derived values are computed by the
// time checker.
struct time_range_t {
  struct value_type {
    int64_t min = 0;
    int64_t max = 0;
  };
  using storage = hhds::flat_storage;
};
inline constexpr time_range_t time_range{};

// PENDING time check: an asserted interval (from an LNAST
// timecheck the discharge pass could not decide, or a mod output's declared
// landing cycle) attached to the checked value's pin. The time checker
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

// Pin-role of each attribute (see Attr_kind). The graph_util pin setters consult
// this to assert the right pin role at the call site. Every attribute is listed
// explicitly so a new one that forgets its kind is a visible omission (the
// default is the most permissive any_pin). `match` is BOTH a node and a driver-pin
// attribute; the entry classifies its PIN overload (driver), and the node overload
// (set_match(node)) takes a Node so it never reaches a pin-role assertion.
template <typename Tag>
inline constexpr Attr_kind attr_kind = Attr_kind::any_pin;

template <>
inline constexpr Attr_kind attr_kind<bits_t> = Attr_kind::driver_pin;
template <>
inline constexpr Attr_kind attr_kind<pin_signed_t> = Attr_kind::driver_pin;
template <>
inline constexpr Attr_kind attr_kind<pin_delay_t> = Attr_kind::driver_pin;
template <>
inline constexpr Attr_kind attr_kind<pin_const_value_t> = Attr_kind::driver_pin;
template <>
inline constexpr Attr_kind attr_kind<match_t> = Attr_kind::driver_pin;
template <>
inline constexpr Attr_kind attr_kind<pin_name_t> = Attr_kind::edge;
template <>
inline constexpr Attr_kind attr_kind<pin_offset_t> = Attr_kind::any_pin;
template <>
inline constexpr Attr_kind attr_kind<time_range_t> = Attr_kind::any_pin;
template <>
inline constexpr Attr_kind attr_kind<pending_time_t> = Attr_kind::any_pin;
template <>
inline constexpr Attr_kind attr_kind<color_t> = Attr_kind::node;
template <>
inline constexpr Attr_kind attr_kind<place_t> = Attr_kind::node;
template <>
inline constexpr Attr_kind attr_kind<hier_color_t> = Attr_kind::node;
template <>
inline constexpr Attr_kind attr_kind<coloring_info_t> = Attr_kind::node;
template <>
inline constexpr Attr_kind attr_kind<proven_t> = Attr_kind::node;
template <>
inline constexpr Attr_kind attr_kind<runtime_check_t> = Attr_kind::node;
template <>
inline constexpr Attr_kind attr_kind<const_value_t> = Attr_kind::node;
template <>
inline constexpr Attr_kind attr_kind<lut_t> = Attr_kind::node;

}  // namespace livehd::attrs

namespace hhds {}  // namespace hhds
