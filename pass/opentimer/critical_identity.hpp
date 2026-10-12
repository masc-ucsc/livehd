// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>

#include "hhds/graph.hpp"

namespace livehd::opentimer {

// Digest of the node identities (definition, id, name, source line) under an
// annotated top. pass.opentimer stores it in the persisted STA summary; `lhd
// tool sta` recomputes it to refuse annotations the graph no longer matches.
std::string critical_identity(const std::shared_ptr<hhds::Graph>& g);

}  // namespace livehd::opentimer
