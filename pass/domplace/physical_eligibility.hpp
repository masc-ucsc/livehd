// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reason.hpp"
#include "semantic_region.hpp"

namespace livehd::domplace {

// The v1 physical eligibility of every frozen state, in frozen order:
// Reason::candidate or the code that retains its CMOS+DFF implementation (the
// README table). A candidate's clock control output must be a non-inverted
// native source that is one of frozen.inputs, and its region input name (from
// `input_names`, indexed like frozen.inputs) must equal `clock`. Enables and
// synchronous resets are already folded into D, so they stay candidates.
// Requires region.frozen; returns no rows without it.
std::vector<std::pair<uint32_t, Reason>> physical_eligibility(const usyn::Stateful_region& region,
                                                              std::span<const std::string> input_names, std::string_view clock);

}  // namespace livehd::domplace
