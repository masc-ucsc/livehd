#pragma once

#include <chrono>

namespace profile_time {
using namespace std::chrono;
class timer {
public:
  void start() { s = system_clock::now(); }

  // time since start
  int64_t time() { return duration_cast<milliseconds>(system_clock::now() - s).count(); }

private:
  system_clock::time_point s;
};
};  // namespace profile_time