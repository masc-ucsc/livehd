// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "clock_bounds.hpp"
#include "inou/lefdef/physical.hpp"
#include "pass/place/pcrs/clock_tree.hpp"
#include "row_occupancy.hpp"
#include "satlegalize.hpp"

namespace livehd::placement {
struct Hold_delay {
  physical::Component               cell;
  std::string                       input, output, source_net, new_net;
  size_t                            net_index;
  std::vector<physical::Connection> sinks;
  double                            area_um2;
  double                            hpwl_delta_um = 0;
};
struct Hold_repair_options {
  double                                 area_um2;
  const liberty::timing::Library&        timing;
  std::vector<const pcrs::Clock_driver*> drivers;
  unsigned                               padding_sites = 0;
  int64_t                                halo_x_dbu = 0, halo_y_dbu = 0;
  const Satlegalize_route_grid*          pin_access = nullptr;
};
void reserve_hold_pin_access(std::vector<Row_gaps>& rows, const Satlegalize_route_grid& grid);
struct Hold_candidate {
  size_t                    net;
  const pcrs::Clock_driver* driver;
  double                    gain_per_area, worst_hold, area;
};
std::vector<Hold_candidate> rank_hold_delays(const physical::Floorplan&, const physical::Technology&, const Hold_repair_options&,
                                             const Endpoint_hold&, double remaining_area, const Endpoint_hold* setup = nullptr,
                                             const std::set<std::pair<size_t, std::string>>& rejected = {});
std::optional<Hold_delay>   plan_hold_delay(const physical::Floorplan&, const physical::Technology&, const pcrs::Clock_driver&,
                                            const Endpoint_hold&, size_t net_index, const std::vector<Row_gaps>* rows = nullptr,
                                            unsigned padding_sites = 0);
void                        insert_hold_delay(physical::Floorplan&, const Hold_delay&);
}  // namespace livehd::placement
