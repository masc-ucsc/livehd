// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "clock_gates.hpp"
#include "diag.hpp"
#include "dlop.hpp"
#include "lnet.hpp"

namespace livehd::synth {

struct Region_blast;

enum class State_scope { logic, memory, opaque };
enum class State_role { register_candidate, transparent_latch, icg, memory, opaque, hierarchy };
enum class State_target { cmos, domino };

// Owned snapshot: hooks may read this while the graph lock is released.
// Local node/port numbers identify controls within source_graph; they are not
// graph handles to dereference or a claim of identity across recompilation.
struct Source_signal {
  uint64_t node     = 0;
  uint32_t port     = 0;
  bool     present  = false;
  bool     constant = false;
  uint32_t bits     = 0;  // effective source width; absent signals have zero
  Dlop     value;
};

struct Source_state {
  uint64_t      node = 0;
  std::string   name;
  diag::Span    span;
  State_role    role = State_role::opaque;
  uint32_t      bits = 0, stages = 0;
  // bits is the blaster's effective width (one for an unannotated pin).
  // Preserve whether source width metadata was actually present.
  bool          width_known = false;
  Source_signal q, data, clock, clock_root, enable, reset, initial;
  bool          neg_clock        = false;
  bool          clock_edge_known = true;
  bool          neg_reset        = false;
  bool          async_reset      = false;
  // An `initial` with a reset is the reset value, never power-on state.
  bool          power_on         = false;
  int32_t       icg              = -1;  // structural clock gate index, independent of a library pick
  uint64_t      translated_bits  = 0;
};

struct Semantic_lid {
  Lid  node     = Lnet::kNone;
  bool inverted = false;
};

struct State_bit {
  uint32_t     source = 0, stage = 0, bit = 0, latch = 0;
  std::string  name;  // source stage/bit spelling; not an ABC-generated CI name
  Semantic_lid q, d;
};

struct Source_clock_gate {
  Source_signal output, clock, enable;
  uint64_t      latch_node = 0;
  int32_t       parent     = -1;
  std::string   name;
  diag::Span    span;
};

enum class State_control_kind { clock, async_reset, initial_value };
struct State_control {
  uint32_t              source = 0;
  State_control_kind    kind   = State_control_kind::clock;
  // Raw control bits, low bit first, as Lnet OUTPUT indices. Shared controls
  // may reference the same outputs. Unlike Semantic_lid these indices survive
  // boundary-preserving logic rewrites/export. Reset polarity stays in source.
  std::vector<uint32_t> outputs;
};

struct Source_state_table {
  std::string                    source_graph;
  State_scope                    scope = State_scope::logic;
  std::vector<Source_state>      sources;
  std::vector<Source_clock_gate> clocks;
  // Crossed bits only. Uncrossed native state is still in sources, so mapping
  // choices cannot hide it or change eligibility. Logical translation includes
  // every register_candidate, even when no Liberty cell exists.
  std::vector<State_bit>         bits;
  // Logical translation keeps clocks and asynchronous events outside D and
  // exposes their nonconstant functions (and dynamic initialization values)
  // as protected combinational outputs.
  // Constants (including unknown reset bits) remain in the owned source row.
  bool                           logical_boundary = false;
  std::vector<State_control>     controls;
};

// An expanded memory module's structural memory_module attribute overrides the
// default logic scope. Names never imply memory or opaque-state exclusions.
Source_state_table inspect_source_state(const partition::Region_body& region, const Clock_gates& clocks,
                                        State_scope scope = State_scope::logic);
// All diagnostics use the snapshotted span, including locationless input.
// CMOS has no DOMINO-only clock/storage restriction.
bool               validate_state_target(const Source_state_table& table, State_target target, std::string_view pass = "pass.usyn");
// Validate reachable source definitions before partitioning/lowering can hide
// a latch or clock polarity behind a native boundary or cached mapped body.
// Opaque definitions remain barriers; shared definitions are visited once.
bool               validate_source_design(std::span<const std::shared_ptr<hhds::Graph>> roots, State_target target,
                                          std::string_view pass = "pass.usyn");
// Connect the RAW Lnet to source rows after blasting. d explicitly undoes the
// backend's QN-only encoding; semantic Q and next-state D remain authoritative.
// Refuse malformed/duplicate correspondence instead of guessing by a name.
bool               bind_source_state(Source_state_table& table, const Region_blast& blast);
// Validate the complete control interface of a logical boundary. Legacy
// mapped snapshots have no such interface and return true only if it is empty.
bool               validate_state_controls(const Source_state_table& table, const Lnet& net);

}  // namespace livehd::synth
