// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "satopt_stages.hpp"
namespace livehd::satopt {
void share_operators(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Stage_report& report, Meter& meter);
}
