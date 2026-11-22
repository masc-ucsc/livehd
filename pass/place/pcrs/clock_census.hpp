// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string>

namespace hhds {
class Graph;
}

namespace livehd::pcrs {
// Structural clock domains and RTL feedback from the mapped hierarchy, before placement.
std::string clock_census(hhds::Graph& top, const std::string& liberty_text);
}  // namespace livehd::pcrs
