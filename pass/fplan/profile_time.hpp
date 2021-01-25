//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <chrono>

namespace profile_time {
using namespace std::chrono;
class Timer {
public:
  void start() { s = system_clock::now(); }

  // time since start
  int64_t time() { return duration_cast<milliseconds>(system_clock::now() - s).count(); }

private:
  system_clock::time_point s;
};
};  // namespace profile_time