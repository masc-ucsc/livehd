//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>

namespace upass {

// Result of a const-replacement on a node: how many sinks were rewired, how
// many fresh const drivers were materialised, and how many nodes were torn
// down. Returned by each manager's `estimate_replace_with_const` /
// `replace_with_const`. Lives here (rather than per-manager) so the templated
// shared passes can name a single type.
struct Replace_effect {
  std::size_t rewired_edges{0};
  std::size_t new_const_nodes{0};
  std::size_t deleted_nodes{0};
};

}  // namespace upass
