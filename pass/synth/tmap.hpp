// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <vector>

#include "unate.hpp"

namespace livehd::synth {

enum class Map_status { mapped, unsupported, exhausted, proof_inconclusive, mismatch, invalid };

struct Input_environment {
  std::string name;
  std::string driving_cell;
  double      arrival_ps = 0;  // zero-load driver-output arrival; at the port if no driver
};
struct Output_environment {
  double load_ff       = -1;
  double downstream_ps = 0;
};
struct Network_environment {
  std::vector<Input_environment>  inputs;                // indexed by source origin
  std::vector<Output_environment> outputs;               // indexed by network output
  double                          internal_load_ff = 1;  // first-pass estimate, replaced with mapped sink caps
};
struct Mapping_request {
  std::vector<Input_environment>     inputs;
  std::vector<std::vector<uint32_t>> terms;
  bool                               inverter = false;
  std::string                        library;
  double                             output_load_ff  = -1;
  double                             required_ps     = -1;
  uint32_t                           proof_conflicts = 100000;
  uint32_t                           max_cells       = 10000;
  std::function<bool()>              admission{};
  // Remaining region wall time. A supplied callback requires killable mapping.
  std::function<double()>            remaining_ms{};
  // Parent-owned aggregate admission while a worker is live; 0 means no sample.
  std::function<bool(uint64_t)>      worker_admission{};
};

// A backend-neutral, topologically ordered cell fragment. Wires [0, inputs)
// are the request's ordered input ports. Cell i produces wire inputs+i.
// cell_sop is a total Boolean function in ordered input-pin order (BLIF SOP).
// It carries the library semantics used for proof and an independent exporter;
// the mapped instance still names the actual library cell and ports.
struct Mapped_cell {
  std::string              name;
  std::vector<std::string> input_pins;
  std::string              output_pin;
  std::vector<Id>          inputs;
  std::string              cell_sop;
  double                   area = 0;
};
struct Mapped_fragment {
  Map_status               status = Map_status::invalid;
  std::string              reason;
  uint32_t                 inputs = 0;
  std::vector<Mapped_cell> cells;
  Id                       output = 0;
  double                   area   = 0;
  std::vector<double>      input_loads_ff;
  double                   output_arrival_ps = -1;
  Input_environment        output_environment;
};

// A multi-output block (pass.synth.split's non-unate remainder): sources are
// the block inputs in order, functions follow topologically, outputs are the
// signals its consumers read. Technology mapping ONLY -- no restructuring.
struct Block_request {
  Logic_network           logic;
  std::string             library;
  uint32_t                proof_conflicts = 100000;
  uint32_t                max_cells       = 1000000;
  std::function<bool()>   admission{};
};
// Wires [0, inputs) are the block sources in order; cell i drives wire
// inputs+i; outputs[k] is the wire of logic.outputs[k] (possibly an input).
struct Mapped_block {
  Map_status               status = Map_status::invalid;
  std::string              reason;
  uint32_t                 inputs = 0;
  std::vector<Mapped_cell> cells;
  std::vector<Id>          outputs;
  double                   area = 0;
};

class Tmap_backend {
public:
  virtual ~Tmap_backend()                                     = default;
  virtual Mapped_fragment map(const Mapping_request& request) = 0;
  // Unsupported unless the backend maps whole blocks.
  virtual Mapped_block    map_block(const Block_request&) {
    return {.status = Map_status::unsupported, .reason = "backend maps single functions only"};
  }
  // INSIGHT ONLY (pass.synth.reference): the LUTs of a k-LUT area mapping of
  // `logic` (after structural choices when `choices`) as (table, inputs)
  // pairs; empty when the backend has none.
  virtual std::vector<std::pair<uint64_t, uint32_t>> lut_reference(const Logic_network&, uint32_t, bool) { return {}; }
};

struct Mapped_network {
  Map_status               status = Map_status::invalid;
  std::string              reason;
  std::vector<Id>          source_origins;
  std::vector<Mapped_cell> cells;
  std::vector<Id>          cell_origins;  // owning Unate_node ID, preserves function boundaries
  std::vector<Id>          outputs;
  double                   area = 0;
};

// Transactional: only a fully mapped network is returned; a failed fragment
// leaves the original unate network untouched. One fragment instance per DAG
// node, including source inverters and demanded twins. This does not perform
// cross-function Boolean optimization or claim whole-design LEC/timing.
// A supplied environment enables two bounded forward mapping sweeps. The
// second uses summed mapped sink capacitances; local arrivals/required times
// guide sizing, and the caller must time the complete stitched result.
Mapped_network map_network(const Unate_network& network, Tmap_backend& backend, const Mapping_request& defaults,
                           const Network_environment* environment = nullptr);

// pass.synth.split: map a Split_result. The remainder is ONE block over the
// sources (block_defaults.logic is filled here), every gate is its own
// function request (defaults), and a leaf taken in the polarity its producer
// does not drive gets one shared inverter. Transactional like map_network.
struct Split_mapping {
  Mapped_network network;
  uint64_t       gate_cells = 0, remainder_cells = 0, inverters = 0;
  double         remainder_area = 0, remainder_ms = 0;
};
Split_mapping map_split(const Logic_network& source, const Split_result& split, Tmap_backend& backend,
                        const Mapping_request& defaults, const Block_request& block_defaults);

}  // namespace livehd::synth
