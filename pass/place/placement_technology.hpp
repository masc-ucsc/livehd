// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "inou/lefdef/physical.hpp"
#include "pass/liberty/liberty_dff.hpp"

namespace livehd::liberty::timing {
struct Library;
}

namespace livehd::placement {
struct Technology {
  std::vector<std::string> lefs;
  std::string              liberty_path;
  std::array<std::string, 2> tie_masters;
  physical::Technology     lef;
  liberty::Cell_library    cells;
  uint64_t                 content_hash = 0;
  Technology(const std::vector<std::string>& paths, const std::string& lib, const std::array<std::string, 2>& ties = {});
  std::shared_ptr<const liberty::timing::Library> clock_library(std::string extra_liberty = {}) const;

private:
  mutable std::mutex clock_library_mutex_;
  mutable std::map<std::string, std::shared_ptr<const liberty::timing::Library>> clock_libraries_;
};
}  // namespace livehd::placement
