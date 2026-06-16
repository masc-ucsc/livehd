//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "hhds/graph.hpp"

namespace livehd::yosys_tolg {

// Gids of the graph bodies built by the most recent `yosys2lg` pass run.
// Yosys2lg_Pass::execute() clears this at entry (reset_built_gids) and appends
// one entry per module body it (re)builds (record_built_gid). do_tolg reads
// built_gids() to collect exactly the graphs produced this run.
//
// Why an explicit channel and not an all_gids() before/after diff: gids are
// stable name-hashes, so when re-running into a previously-saved lgdb the
// module's slot is already loaded from disk and the diff sees nothing "new"
// (every produced gid was already in the before-set) -> var.graphs ends up
// empty -> "verilog elaboration produced no graphs".
//
// Single-threaded: the yosys run is serial and fully bracketed by one do_tolg
// call, so a file-static backing vector is safe.
void                          reset_built_gids();
void                          record_built_gid(hhds::Gid id);
const std::vector<hhds::Gid>& built_gids();

}  // namespace livehd::yosys_tolg
