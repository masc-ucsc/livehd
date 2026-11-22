// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "pass/place/pcrs/clock_tree.hpp"
#include "place.hpp"
#include "technology_rc.hpp"

namespace livehd::placement {
struct Clock_planning_model {
  std::vector<pcrs::Clock_driver> drivers;
  Net_wire_rc                     wires;
  Wire_rc                         leaf_rc, trunk_rc;
  pcrs::Clock_limits              limits;
};
Clock_planning_model clock_planning_model(const Options&, const liberty::timing::Library&, double dbu);
struct Clock_chunk_homes {
  std::map<std::pair<bool, std::string>, std::string>       members;
  std::map<std::string, std::vector<std::array<double, 4>>> regions;
  explicit Clock_chunk_homes(const std::shared_ptr<const std::string>& plan);
  std::string locate(bool macro, std::string_view key, Point pos) const;
  std::string home(const physical::Connection&, const std::map<std::string, hhds::Node_class, std::less<>>& nodes,
                   const std::map<std::string, const physical::Component*, std::less<>>& instances, bool macro, Point pos) const;
};
}  // namespace livehd::placement
