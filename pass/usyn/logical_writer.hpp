// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "artifact.hpp"
#include "semantic_region.hpp"

namespace livehd::usyn {

// Standalone bit-level CMOS module. Ports correspond, in order, to the
// selected logic's inputs/outputs, including protected state controls. Native
// memory/ICG/opaque boundaries are external to this module: the region adapter
// must connect their bit ports and rebuild those native structures separately.
// Own the library until the caller copies/embeds the graph in its output.
struct Logical_module {
  hhds::GraphLibrary            library;
  std::shared_ptr<hhds::Graph>  graph;
  std::vector<std::string>      inputs, outputs;
  std::vector<hhds::Node_class> state;
};

struct Logical_module_result {
  Status                          status = Status::invalid;
  std::unique_ptr<Logical_module> module;
  std::string                     reason;
};

// Emit the complete behavioral CMOS expansion, retaining every original
// register bit/stage, event polarity and asynchronous/resetless initial value.
// Synchronous reset/enable are already folded into D. Phase-1 selected cells
// add no state. No Liberty, ABC or prover is involved. Requires the complete
// logical control interface, not a legacy mapping-dependent source snapshot.
// Refusal destroys the private partial module and publishes no graph.
// max_nodes bounds the input and a conservative emitted-node reservation.
Logical_module_result write_logical_module(const Stateful_region& region, std::string_view name, Budget& work,
                                           uint32_t max_nodes = 2000000);
// Replay a validated standalone artifact. The output is behavioral CMOS even
// when the recorded target is DOMINO; this is not a physical cell emitter.
Logical_module_result write_logical_module(const Frozen_region& region, Budget& work, uint32_t max_nodes = 2000000);

}  // namespace livehd::usyn
