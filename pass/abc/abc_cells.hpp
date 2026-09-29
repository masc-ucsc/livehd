// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// ABC's mapped results in the backend-neutral vocabulary of pass/synth
// (cell_netlist.hpp): the frame's GENLIB as a Cell_library, and a mapped ABC
// netlist as a Cell_netlist the region writer reads back.
#include <optional>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "cell_netlist.hpp"

namespace livehd::abc {

// Mio gate (Mio_Gate_t*) -> its Cell_library type.
using Gate_types = absl::flat_hash_map<const void*, uint32_t>;

// Every gate of a Mio library (Mio_Library_t*), in library order, and the
// library's inverter.
void build_cell_library(void* mio_library, synth::Cell_library& lib, Gate_types& types);

// A mapped ABC netlist (Abc_Ntk_t*, ABC_NTK_NETLIST with a Mio mapping): its
// PIs, latches and POs in ABC's CI/CO order -- the translation's creation
// order -- and every mapped node as a cell. nullopt (diagnosed) when a node
// carries no Mio gate.
std::optional<synth::Cell_netlist> abc_to_cells(void* netlist, const Gate_types& types, std::string_view region);

}  // namespace livehd::abc
