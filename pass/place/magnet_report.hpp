// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string>
#include <vector>

#include "dreamplace/native/magnet.hpp"
namespace livehd::placement {
void write_magnet_report(const DreamPlace::native::MagnetReport&, const std::vector<std::string>& names, int64_t dbu,
                         const std::string& directory);
}
