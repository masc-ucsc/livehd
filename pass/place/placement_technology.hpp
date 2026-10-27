// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "inou/lefdef/physical.hpp"
#include "pass/liberty/liberty_dff.hpp"

namespace livehd::placement {
struct Technology {
  std::vector<std::string> lefs;
  std::string              liberty_path;
  physical::Technology     lef;
  liberty::Cell_library    cells;
  uint64_t                 content_hash = 0;
  Technology(const std::vector<std::string>& paths, const std::string& lib);
};
}  // namespace livehd::placement
