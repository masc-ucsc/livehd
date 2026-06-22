//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Case-insensitive std::string-keyed container aliases. LiveHD/Pyrope match
// names (modules, lambdas, imports, variables, fields) case-insensitively while
// preserving the original spelling: the first key inserted wins as the stored
// key (used for emission), and later lookups in any case fold onto it. The
// folding primitives + functors live in core/str_tools.hpp (absl-free); this
// header only binds them to the absl containers.

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "str_tools.hpp"

template <typename V>
using Ci_str_map = absl::flat_hash_map<std::string, V, str_tools::Ci_hash, str_tools::Ci_eq>;

using Ci_str_set = absl::flat_hash_set<std::string, str_tools::Ci_hash, str_tools::Ci_eq>;
