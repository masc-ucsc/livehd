// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <cmath>

#include "job_manager.hpp"
namespace livehd::sim {
inline unsigned available_cpus() { return livehd::available_cpus(); }
inline unsigned available_compile_jobs() {
  const auto h = livehd::host_resources();
  return std::min(livehd::Job_manager::global().ceiling(),
                  static_cast<unsigned>(std::max(1.0, static_cast<double>(h.cpus) - std::ceil(h.load))));
}
}  // namespace livehd::sim
