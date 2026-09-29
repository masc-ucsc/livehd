// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace livehd::abc {

// Fully resolved commands and policy for one region's baseline mapping. No
// graph handles or callbacks are stored here, so the same plan can travel to
// an isolated ABC worker without duplicating the mapping policy.
struct Flow_plan {
  std::string flow, size_to_budget, map_step, remap_post, area_flow;
  bool        ladder = false, remappable = false, area_candidate = false;
  float       budget         = 0;
  uint32_t    area_relax_pct = 0;
};
using Flow_qor = std::optional<std::pair<float, double>>;
enum class Flow_status { completed, refused, failed };
enum class Flow_refusal { none, time, memory };
struct Flow_result {
  Flow_status  status = Flow_status::completed;
  std::string  stage, command, candidate;
  Flow_qor     delay_qor, area_qor;
  uint32_t     commands_completed = 0;
  Flow_refusal refusal            = Flow_refusal::none;
  std::string  observation_json;  // optional current-invocation worker accounting
};

// Caller owns and has entered the ABC frame, loaded the library, installed
// boundary timing, and set the original logic network as its current network.
// The frame owns the selected result. Temporary candidate networks are released
// on every exit. Admission surrounds commands and timing/copy steps; it is
// cooperative and cannot interrupt a running ABC call.
Flow_result execute_flow(void* frame, const Flow_plan& plan, const std::function<bool(std::string_view)>& admission = {});
Flow_qor    physical_flow_qor(void* mapped);

// The one library-capability predicate for every SCL command (the `buffer`/
// `dnsize` tails, the boundary re-size and the SCL QoR timer): the parsed
// Liberty (an SC_Lib*) carries 2-D NLDM slew/load surfaces. See abc_flow.cpp.
bool lib_has_nldm_timing(const void* scl_lib);
// lib_has_nldm_timing of the entered frame's SCL library.
bool frame_has_nldm_timing();

// Convert measured slack to ABC's bounded area-recovery percentage.
int area_relax_percent(float target, float achieved, uint32_t cap);
}  // namespace livehd::abc
