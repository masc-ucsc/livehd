//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The per-node source attributes that used to live here — `lnast.loc`
// (pos1/pos2/line/tok struct) and `lnast.fname` (an uninterned per-node
// std::string path) — are gone. Source provenance is one uint64
// SourceId per def-bearing node (hhds::attrs::srcid), resolved through the
// Lnast's hhds::Source_locator (hhds::Source_locator). The token text still
// rides on hhds::attrs::name.

#include "hhds/attrs/srcid.hpp"
