// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The per-bit mux facts of satopt (pass/satopt/satopt_mux.hpp) proven with ABC
// `&fraig`: the proof network is replayed into a private ABC frame.
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"
#include "lnet.hpp"
#include "satopt_mux.hpp"

namespace livehd::abc {
using livehd::satopt::Mux_fact;
using livehd::satopt::Mux_satopt;
using livehd::satopt::Satopt_result;

// Which outputs of `net` ABC sweeps to constant zero (`&fraig -x -C 500`), in
// output order; nullopt when no ABC frame is available.
std::optional<std::vector<bool>> prove_const0(const synth::Lnet& net);

// satopt::mux_satopt / satopt::optimize_muxes with the ABC prover.
std::shared_ptr<const Satopt_result> satopt(hhds::Graph* graph, std::string_view cache_dir = {}, bool all_regions = false);
Mux_satopt                           optimize_muxes(hhds::Graph* graph, std::string_view cache_dir = {}, bool all_regions = false);
}  // namespace livehd::abc
