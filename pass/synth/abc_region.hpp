// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <optional>
#include <string>

#include "abc_map.hpp"
#include "tmap.hpp"
#include "unate.hpp"
#include "witness.hpp"

namespace livehd::synth {
// ABC CI/CO classification for the exact network imported as a Boolean source.
// Refuses malformed/oversized metadata; no clock or physical claims are inferred.
std::optional<Source_boundary> abc_source_boundary(void* network, const std::function<bool()>& admission = {});
// A region's blast tape as the optimizer's source network, with no ABC logic
// synthesis in between: sources in CI order (PIs, then latch outputs), the
// constant, then the structurally hashed gates; one output per CO (POs, then
// latch inputs). A complemented edge folds into its consumer's table. An XOR
// stays one 2-input node when `xor_nodes`, else it becomes three ANDs.
// Returns an empty network when `admit` refuses.
Logic_network import_tape(const livehd::abc::Blast_tape& tape, bool xor_nodes, const std::function<bool()>& admit = [] { return true; });
// Map one region through the unate optimizer: decompose the region's logic
// (`tape`, or when null `original` through strash) into the fewest unate
// functions, technology-map each through `backend`, stitch into `original`'s
// PI/PO/latch skeleton, and install the mapped network as the frame's
// current network. On any failure the frame is left untouched, so pass.abc
// maps the region with its own flow; the returned JSON row says which and why.
// The optimizer and tmap interface stay independent of ABC's graph types.
std::string map_abc_region(void* frame, void* original, const livehd::abc::Blast_tape* tape, std::string_view region,
                           const livehd::abc::Map_options& mapping, const Search_options& search, float budget_ps,
                           livehd::abc::Map_options::Alternative_resources resources, Tmap_backend& backend,
                           Witness_archive& witnesses);
}  // namespace livehd::synth
