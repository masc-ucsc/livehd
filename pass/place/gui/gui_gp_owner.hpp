// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>

#include "dreamplace/native/placement.hpp"

namespace livehd::placement::gui {
// An internal serial GP recipe, starting at solver entry. Never reconstruct an
// in-flight optimizer from a Frame. The owner forks from the actual callback.
void serve_native_gp(int fd, const std::string& inputs, std::set<pid_t>& children, bool (*single_thread)(), bool hosted = false);
// Only usable inside a verified, unedited interactive poll in the hosted primary.
bool native_gp_available();
bool retain_native_gp(const std::string& endpoint, uint64_t identity);
// Thrown only by an already-forked candidate to return to its own GP stack.
// The owner and main stay at their original callback.
struct Native_gp_resume {
  DreamPlace::native::InteractUpdate                           patch;
  std::function<void(const DreamPlace::native::InteractView&)> finish;
  std::function<void(const std::string&)>                      fail;
};
// Returns nullopt only before spawning, when the ordinary local solver is needed.
std::optional<DreamPlace::native::Result> run_native_gp(const std::string& executable, DreamPlace::native::Design&,
                                                        const DreamPlace::native::Options&, std::string& unavailable,
                                                        const std::function<bool()>& idle_pulls = {});
}  // namespace livehd::placement::gui
