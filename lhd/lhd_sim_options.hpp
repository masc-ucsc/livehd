//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The `sim.*` option grammar shared by `lhd sim` and `lhd compile --emit-dir sim:` (what is left of the old
// profile-guided tuner session, which went with the generated-C++ driver): the value grammar of every `sim.<flag>`,
// the renamed spellings, and the inou.cgen.sim labels of the resolved tune vector.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lhd.hpp"

namespace lhd {

// The value grammar of one `sim.<flag>=value` (check_known_set_passes). Returns {message, hint}; an empty message
// means the value is valid.
std::pair<std::string, std::string> sim_set_value_error(const Sim_set_option& opt, std::string_view value);

// An old sim.* spelling (sim.color_dirty, sim.fence_ratio, sim.live_words, sim.backend, sim.vcdfakedelay) -> the
// replacement `sim.<flag>=<value>` in the new grammar, ready for a "use --set X instead" hint.
std::optional<std::pair<std::string, std::string>> renamed_sim_set(std::string_view flag, std::string_view value);

// The inou.cgen.sim labels of the resolved tune vector (color_dirty, fence_ratio, live_words, backend), for
// `lhd compile --emit-dir sim:`: explicit --set > positional simulation tuning > default. Throws Lhd_error on a bad
// tune file. A positional simulation tuning is an input of the generated code: it joins res.inputs.
std::vector<std::pair<std::string, std::string>> sim_tune_codegen_labels(const Options& opts, Result& res);

}  // namespace lhd
