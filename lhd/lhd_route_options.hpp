// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

namespace lhd {
enum class Route_mode { flat = 1, chunk = 2, repair = 4, verify = 8 };
enum class Route_value { flag, integer, number, order, grid, indices, path };
struct Route_setting {
  std::string_view name;
  int              modes;
  Route_value      type;
  std::string_view help;
};
inline constexpr int           flat_route       = static_cast<int>(Route_mode::flat);
inline constexpr int           chunk_route      = static_cast<int>(Route_mode::chunk);
inline constexpr int           repair_route     = static_cast<int>(Route_mode::repair);
inline constexpr Route_setting route_settings[] = {
    {            "trace",                              flat_route,    Route_value::path,                                                       "Trace routing of this net."},
    {         "aim_tree",                              flat_route,    Route_value::flag,                              "Aim each branch search at the existing routed tree."},
    {         "prelegal",                              flat_route,    Route_value::flag,                       "Legalize movable cells on demand from their initial poses."},
    {        "tile_move",                              flat_route,    Route_value::flag,                      "Move blocked cells to legal sites in a nearby routing tile."},
    {          "reserve",                              flat_route,    Route_value::flag,                                                "Reserve pin access while routing."},
    {     "trunks_first",                             chunk_route,    Route_value::flag,                          "Route crossing and clock nets before routing interiors."},
    {         "all_pins",                            repair_route,    Route_value::flag,                        "Release nearby wires around all endpoints of failed nets."},
    {    "reserve_first",                            repair_route,    Route_value::flag,                      "Reserve compatible M1 access pads before the repair cohort."},
    {    "pin_neighbors",                            repair_route,    Route_value::flag,                                            "Reopen incident nets at blocked pins."},
    {"pin_access_layers",                            repair_route,    Route_value::flag,                                    "Limit spatial access repair to M1 through M3."},
    {           "margin",                              flat_route, Route_value::integer,                                       "Minimum search-window margin in DEF units."},
    {            "limit",                              flat_route, Route_value::integer, "Maximum nets to attempt in a flat pass. Zero produces a verification checkpoint."},
    {            "ripup",                              flat_route, Route_value::integer,                                                         "Maximum rip-ups per net."},
    {        "rip_width",                              flat_route, Route_value::integer,                                "Maximum nets ripped up after one routing failure."},
    {           "budget",                              flat_route, Route_value::integer,                                                       "Maximum search expansions."},
    {            "shift",                              flat_route, Route_value::integer,                                      "Maximum pin-access slide attempts per cell."},
    {      "shift_reach",                              flat_route, Route_value::integer,                                                    "Maximum sites per cell slide."},
    {       "flop_reach",                              flat_route, Route_value::integer,                                             "Maximum sites a clock sink may move."},
    {        "shift_cap",                              flat_route, Route_value::integer,                        "Maximum displacement, in sites, from the input cell pose."},
    {            "chain",                              flat_route, Route_value::integer,                               "Maximum neighboring cells in a site-clearing push."},
    {     "final_passes",                              flat_route, Route_value::integer,                                    "Extra routing passes over the remaining nets."},
    {          "explain",                              flat_route, Route_value::integer,                                               "Number of failed pins to diagnose."},
    {    "space_min_len",                              flat_route, Route_value::integer,                             "Minimum wire length for extra spacing, in DEF units."},
    {  "space_min_layer",                              flat_route, Route_value::integer,                                         "Minimum routing layer for extra spacing."},
    {        "net_count",                            repair_route, Route_value::integer,                                       "Maximum signals selected for a timing ECO."},
    {           "radius",                            repair_route, Route_value::integer,                            "Neighborhood radius around failed pins, in DEF units."},
    {          "m2_cost",                              flat_route,  Route_value::number,                                                           "Relative M2 wire cost."},
    {          "m3_cost",                              flat_route,  Route_value::number,                                                           "Relative M3 wire cost."},
    {          "hweight",                              flat_route,  Route_value::number,                                                  "A-star search heuristic weight."},
    {          "hi_cost",                              flat_route,  Route_value::number,                                                    "Relative M6 and M7 wire cost."},
    {         "via_cost",                              flat_route,  Route_value::number,                                                        "Cost of a via transition."},
    {        "long_frac",                              flat_route,  Route_value::number,                                      "Fraction of longest data nets routed first."},
    {       "space_frac",                              flat_route,  Route_value::number,                      "Fraction of worst-slack data nets given extra wire spacing."},
    {       "time_limit",                              flat_route,  Route_value::number,                     "CPU seconds for the routing loop. Zero removes the deadline."},
    {      "anchor_frac",                              flat_route,  Route_value::number,             "Fraction of worst-slack nets whose cells become fixed after routing."},
    {        "crit_frac",                flat_route | chunk_route,  Route_value::number,                        "Fraction of ordinary nets prioritized by input pin slack."},
    {         "pd_alpha", flat_route | chunk_route | repair_route,  Route_value::number,                                                 "Tree path-delay weight in [0,1]."},
    {   "worker_seconds",                             chunk_route,  Route_value::number,                                                    "Routing CPU seconds per home."},
    {   "finish_seconds",                             chunk_route,  Route_value::number,                                         "Routing CPU seconds for the coordinator."},
    {          "seconds",                            repair_route,  Route_value::number,                                             "Routing CPU seconds per repair pass."},
    {            "order",                              flat_route,   Route_value::order,                                         "Net order: short, long, pins or fewpins."},
    {             "grid",                             chunk_route,    Route_value::grid,             "Routing homes NXxNY. Default is the measured two- or four-home grid."},
    {          "release",                             chunk_route, Route_value::indices,                        "Comma-separated home indices returned to the coordinator."},
    {     "agent_socket",                             chunk_route,    Route_value::path,                            "Serve the resident routing agent on this Unix socket."},
};
}  // namespace lhd
