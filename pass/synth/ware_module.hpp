// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>
#include <set>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::synth {
// Source sites of the instances of one ware module, by module name. A ware
// module is shared by every operation of the same shape, so its body has no
// single source line; the instances in the parents do. Tuning reports use these
// to say WHERE a learned choice would be pinned. Informational only (it never
// changes a result); filled by build_ware_modules, one entry per distinct line.
std::vector<hhds::Source_span> ware_sites(std::string_view module);

struct Ware_policy {
  bool arith = true;
  bool cmp   = true;
  bool shift = true;
};
// Persisted pass.color options, independent of the stop_* boundary controls.
Ware_policy                               ware_policy(const hhds::Graph& graph, Ware_policy fallback = {});
// Specialize arithmetic definitions by all operand/result widths and signs.
// Constants and source section options belong to the specialization as well.
// The section COLOR is part of the identity only for a color with its own
// options (an embedded coloring_info region_opts entry, or one listed in
// `option_colors` -- the CLI region_opts keys): color numbers shift when an
// edit adds or removes a region, and naming every width specialization after
// one would rename the callee of every unrelated instance.
//
// `zero_bits` (pass.abc.ware_zero_bits): a shifter's data operand whose parent
// provably drives some bits with 0 (a lane placed by a constant left shift, a
// value narrower than the port) is cleared on those bits INSIDE the module, so
// the mapper sees the constants the module boundary would otherwise hide (only when at
// least an eighth of the operand's bits are provably 0). The
// cleared-bit pattern is part of the module's identity.
std::vector<std::shared_ptr<hhds::Graph>> build_ware_modules(const std::vector<std::shared_ptr<hhds::Graph>>& graphs,
                                                             Ware_policy                                      fallback      = {},
                                                             const std::set<int>&                             option_colors = {},
                                                             bool                                             zero_bits     = false);
}  // namespace livehd::synth
