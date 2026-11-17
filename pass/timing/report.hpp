// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "hhds/graph.hpp"
#include "core/physical_rc.hpp"

namespace livehd::timing::fast {

// The synthesis timing report (`lhd synth`'s timing.json) from the fast engine (the same Physical_ranker placement
// ranks with): the mapped design `top` of `library`, flattened in memory when it is hierarchical (a scratch def,
// removed again), timed on one Liberty corner with the placed parasitics when given, else with the synthesis wire
// estimate below (zero wire without one). The JSON has the
// schema pass.opentimer writes (schema_version, kind "sta", time_unit, designs[0] with area, cells, max_delay,
// critical_pin/src, path, endpoints and the per-region `colors` rows), plus "engine":"fast" and the "unsupported"
// list: what the engine does not time yet (latches, memories, clock gates, ...) is listed there, never dropped
// silently.
//
// Without an SDC every input port whose net reaches a Liberty clock pin (through identity cells) is an ideal clock,
// inputs launch at 0, and every output port is an endpoint with `io_load_ff`; an SDC output without set_load gets
// that load too. Throws std::runtime_error when the design or the library cannot be bound.
struct Report_inputs {
  std::string liberty_text;  // the setup corner: synth.liberty plus the macro libraries, concatenated
  std::string corner_name;
  std::string sdc_text;  // empty: no constraint file
  float       io_load_ff   = 0;
  double      time_unit_ps = 1000;  // the Liberty time unit the report is written in (ns: 1000)
  // The synthesis wire estimate (no placement): an average wire R and C per um and a fanout -> length predictor
  // (um, extended past its last entry by `wire_slope_um`). Every net gets that much wire as a star (driver, the wire
  // resistance, its capacitance at one junction, every sink). Empty lengths: zero wire.
  double                     wire_cap_ff_um = 0, wire_res_ohm_um = 0, wire_slope_um = 0;
  std::map<unsigned, double> wire_length_um;
};
std::string report_json(hhds::GraphLibrary& library, std::string_view top, const Report_inputs& inputs,
                        const std::shared_ptr<const physical::Parasitics>& placed = nullptr);

}  // namespace livehd::timing::fast
