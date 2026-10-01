// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "semantic_region.hpp"

namespace livehd::usyn {

// Portable region artifact, independent of search temporaries, graph handles,
// ABC and mapping libraries. Native barriers/hierarchy are reconnected by the
// design owner; this records the complete logical region boundary.
struct Frozen_region {
  static constexpr uint32_t version = 1;
  std::string               module_name;
  synth::State_target       target = synth::State_target::cmos;
  Endpoint_netlist          netlist;
  synth::Source_state_table source;
  std::vector<uint32_t>     state_bits;
};

struct Artifact_limits {
  uint64_t bytes         = 256ULL * 1024 * 1024;
  uint64_t decoded_bytes = 512ULL * 1024 * 1024;  // conservative storage admission, separate from encoded size
  uint64_t objects       = 16000000;              // aggregate decoded vector elements, before allocation
  uint32_t nodes         = 2000000;
  uint32_t string_bytes  = 1048576;
  uint32_t formula_nodes = 16384;
};

struct Artifact_bytes {
  Status      status = Status::invalid;
  std::string bytes, reason;
};
struct Artifact_result {
  Status                       status = Status::invalid;
  std::optional<Frozen_region> region;
  std::string                  reason;
};

// Format version 1. Exact-length little-endian fields, explicit enum/boolean
// validation and bounded allocation; no raw C++ object representation. Failed
// reads/writes publish no partial artifact. The source/control wrapper is
// checked against the validated behavioral expansion on both paths.
Artifact_bytes  serialize_artifact(std::string_view module, synth::State_target target, const Stateful_region& region, Budget& work,
                                   const Artifact_limits& limits = {});
Artifact_bytes  serialize_artifact(const Frozen_region& region, Budget& work, const Artifact_limits& limits = {});
Artifact_result deserialize_artifact(std::string_view bytes, synth::State_target requested_target, Budget& work,
                                     const Artifact_limits& limits = {});

// Cache envelope: the independently reloadable artifact plus the selected
// endpoint decisions and complete search evidence. `credit` is the search's
// Budget::credit_floor(): its consumed work (replayed on a hit to keep later
// regions' credits, not repeated computation), its floor and whether it was
// bound, with the exact credits of a bound search. `structural` is the
// region's structural-allowance work (import, logical admission and identity
// baseline, freezing), replayed on a hit so warm structural totals equal cold
// ones. A loaded record is checked for consistency (work <= floor, a bound
// record's credits cover both, structural covers report.work.admission).
struct Selection_record {
  std::string     module_name;
  Stateful_region selected;
  Logical_report  report;
  Credit_floor    credit{};
  uint64_t        structural = 0;
};
struct Selection_record_result {
  Status                          status = Status::invalid;
  std::optional<Selection_record> record;
  std::string                     reason;
};
Artifact_bytes          serialize_selection_record(std::string_view key, std::string_view module, const Stateful_region& selected,
                                                   const Logical_report& report, const Credit_floor& credit, uint64_t structural,
                                                   Budget& work, const Artifact_limits& limits = {});
Selection_record_result deserialize_selection_record(std::string_view bytes, std::string_view key, synth::State_target target,
                                                     Budget& work, const Artifact_limits& limits = {});
// User-visible names a region binds, supplied by the design owner. Top-level
// design IO and state names are what equivalence checking matches, so they
// enter the logical identity; other names do not. Empty vectors mean "none";
// a non-empty inputs/outputs/barriers vector must match raw.inputs(),
// raw.outputs() or source.sources exactly.
struct Identity_names {
  std::vector<std::string> inputs;    // per RAW PI: the top-level input bit it reads, or empty
  std::vector<std::string> outputs;   // per RAW PO: the top-level output bit(s) it drives, or empty
  std::vector<std::string> ports;     // top-level IO on region ports without a logical position
  std::vector<std::string> barriers;  // per snapshot row without Q (memory/instance/clock cell): its name
};

// Framed identity of exactly the logical input and policy. Excludes Liberty,
// ABC/tmap settings and mapping producer identity. Includes all native search
// knobs, source controls, register/latch names and `names`. Search credits are
// excluded: the stored record's credit floor decides reuse. Definition/module
// names, source spans, the source graph name, local node numbers (only their
// equalities are kept) and every other region boundary-net name are excluded:
// a hit rebinds them to the fresh translation.
Artifact_bytes serialize_logical_identity(const synth::Lnet& raw, const synth::Source_state_table& source,
                                          const Identity_names& names, const Logical_options& options, uint64_t producer,
                                          std::string_view context, Budget& work, const Artifact_limits& limits = {});

// Rebind a loaded record to the fresh translation of the same identity:
// `module`, the RAW port spellings in its frozen netlist and expansion, and the
// complete fresh source snapshot (source graph, spans, node numbers, barrier
// names). Ports bind by position, so functions, decisions, evidence and state
// names are unchanged. A snapshot that differs in an identity field, or whose
// imported state names/domains differ from the record, is refused as invalid
// (never guessed); resource refusal is search_exhausted. `record` is unchanged
// on refusal.
Status rebind_selection(Selection_record& record, std::string_view module, const synth::Lnet& raw,
                        const synth::Source_state_table& source, Budget& work, const Artifact_limits& limits = {});

}  // namespace livehd::usyn
