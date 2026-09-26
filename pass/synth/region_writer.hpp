// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// A mapped region (Cell_netlist) written back into its region body: each cell
// becomes a 1-bit black-box Sub named after its library cell, registers become
// DFF-cell Subs or native flops under their source names, native boundaries
// are rebuilt and reconnected, and multi-bit ports are reassembled. Backend
// neutral: the backend only converts its mapped network (abc_cells.cpp).
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "cell_netlist.hpp"
#include "hhds/graph.hpp"
#include "liberty_dff.hpp"
#include "pass_partition.hpp"
#include "region_blast.hpp"

namespace livehd::synth {

class Region_writer {
public:
  // What the written netlist holds, updated in place: minted cells and their
  // library area, and the identity buffers aliased away.
  struct Counts {
    int    gates    = 0;
    double area     = 0.0;
    int    bypassed = 0;
  };
  // The register mapping: whether registers crossed as latches, and the DFF
  // cell with its drive ladder (none: registers stay native flops).
  struct Registers {
    bool                                  map = true;
    const std::optional<liberty::Dff_cell>* cell   = nullptr;
    const std::vector<liberty::Dff_cell>*   ladder = nullptr;
    // Asynchronous clear (0) / preset (1) cell ladders; null or empty = none.
    const std::vector<liberty::Dff_cell>*   areset0 = nullptr;
    const std::vector<liberty::Dff_cell>*   areset1 = nullptr;
  };

  void set_outlib(hhds::GraphLibrary* outlib) { outlib_ = outlib; }
  // Whole-design flatten emits exactly one module: no shared input splitters.
  void set_flat(bool flat) { flat_ = flat; }

  // Writes `mapped` into rb.body. False when the region cannot be read back
  // (diagnosed).
  bool write(const livehd::partition::Region_body& rb, const Region_blast& blast, const Cell_netlist& mapped, const Cell_library& lib,
             const Registers& registers, Counts& counts, const std::function<void(std::string_view)>& trace_stage);

private:
  hhds::GraphLibrary* outlib_ = nullptr;
  bool                flat_   = false;
  // ABC is bit-level, but a partition boundary is a packed LGraph bus. A
  // naive read-back emits one constant SRA per input bit in EVERY region. Rob
  // has hundreds of regions reading the same 10k-bit bus, so that duplicates
  // millions of identical unpacking nodes. Keep one native unpacker definition
  // per width and instantiate it from each mapped region instead.
  //
  // The def is all-or-nothing (every declared output pin must exist on the
  // instance), so a region uses it only when it demands most of the bus --
  // see the three gates in write()'s `input_bit`.
  struct Input_splitter {
    std::shared_ptr<hhds::GraphIO> io;
    std::vector<hhds::Port_id>     bit_port;
  };
  absl::flat_hash_map<int, Input_splitter> input_splitters_;
};

}  // namespace livehd::synth
