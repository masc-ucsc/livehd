// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <map>
#include <string>

#include "physical.hpp"

namespace livehd::physical {
// LEF 5.8 cell library holding only MACRO abstracts, read after the technology
// LEF: VERSION/BUSBITCHARS/DIVIDERCHAR, the macros in name order, END LIBRARY.
// It has no UNITS (read_lef would replace the technology DBU) and no SITE.
// Writes the modeled Macro fields: CLASS, ORIGIN, SIZE, SYMMETRY, SITE, PIN
// DIRECTION/USE with one PORT of RECTs, and OBS RECTs. Every coordinate must be
// an integral DBU and is printed exactly. Throws std::invalid_argument for
// geometry the model holds only conservatively (polygon bounds, OBS layers
// without rectangles), empty pins or rectangles and non-LEF names.
// virtual_proxy is a runtime marker and is not written.
std::string write_lef_macros(const std::map<std::string, Macro>& macros, int64_t dbu);
}  // namespace livehd::physical
