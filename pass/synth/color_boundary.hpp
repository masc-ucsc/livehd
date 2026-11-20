// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

#include "hhds/graph.hpp"

namespace livehd::synth {

// What trim_color_boundaries did to one graph.
struct Color_boundary_stats {
  uint64_t constant_values = 0;  // crossing values whose readers of another color now read their known bits
  uint64_t constant_bits   = 0;  // the bits known (summed per crossing value)
  uint64_t unread_values   = 0;  // crossing values handed over through a mask of the bits something reads
  uint64_t unread_bits     = 0;  // the bits those masks clear
  void     add(const Color_boundary_stats& o) {
    constant_values += o.constant_values;
    constant_bits   += o.constant_bits;
    unread_values   += o.unread_values;
    unread_bits     += o.unread_bits;
  }
};

// Nothing constant and nothing unread crosses a color (synth.boundary_bits, on by default). Run on a colored graph
// right before the partitioner cuts it: a region reads another region's value as free inputs, so without this
//  * a bit the producer makes constant is unknown to the reader (a lane of a packed bus that is always 0, a tied
//    valid), and the reader builds logic for it;
//  * a bit no reader asks for is still computed and exported by the producer.
// Constants: structural known bits (bit_facts::known_bits) of every unsigned combinational value read by a cell of
// another color; each reading color reads `(value & keep) | ones` through cells of its own color (the constant itself
// when every bit is known). Unread bits: mask_unread_color_outputs (state_sweep.hpp). No proof search: both are
// linear walks, and every reader keeps the value it read on every bit it reads.
// Duplicate cells are not this step's business: the e-graph fold shares them (hash-consing, its `cse_merged` count) in
// every fold of every flow.
Color_boundary_stats trim_color_boundaries(hhds::Graph* graph);

}  // namespace livehd::synth
