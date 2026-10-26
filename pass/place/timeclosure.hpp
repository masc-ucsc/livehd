// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace livehd::placement::timeclosure {
// Observation/controller types deliberately have no timer or placer dependency.
// All timing values in one history must use the same units and timing model.
enum class Metric : unsigned { period, window_tns, hpwl, opaque_rudy, count };
inline constexpr size_t metric_count = static_cast<size_t>(Metric::count);
struct Settings {
  int    budget = 800, sample_interval = 10, window = 5;
  double horizon = 50, range_fraction = .03, noise_multiplier = 2;
  double endpoint_overlap = .9;
  bool   memory           = false;
  int    memory_growth = 25, memory_settle = 20, rudy_dwell = 20;
  int    timing_response = 20, repair_dwell = 20;
  int    correction_max_age       = 50;
  double correction_overflow_drop = .1;
  void   validate() const;
};
struct Sample {
  int                                             iteration = 0;
  std::array<std::optional<double>, metric_count> values;
  std::optional<double>                           density_overflow;
  std::optional<std::set<std::string>>            endpoints;
  // Changes invalidate histories, but never rewind the budget or latches.
  uint64_t                                        topology = 0, geometry = 0, timing_model = 0;
  bool                                            virtual_timing = false;
};
struct Trend {
  bool   ready = false, pass = false, stable = false, turning = false;
  double value = 0, slope = 0, range = 0, noise = 0;
  int    samples = 0;
};
// Range covers this epoch's history; slope/noise cover only its last N samples.
class History {
  std::vector<std::pair<int, double>> points;
  bool                                previous_pass  = false;
  size_t                              evaluated_size = 0;
  Trend                               latest;

public:
  void  reset();
  void  append(int iteration, std::optional<double> value);
  Trend evaluate(const Settings& settings);
};
double jaccard(const std::set<std::string>& a, const std::set<std::string>& b);

enum class Gate { memory, rudy, timing, repair, finish, correction, reset };
enum class Reason { stable, deadline, budget, precondition, age, overflow, epoch, complete };
const char* name(Gate gate);
const char* name(Reason reason);
const char* name(Metric metric);
struct Event {
  Gate                            gate;
  Reason                          reason;
  int                             iteration = 0, dwell_until = 0, remaining = 0;
  bool                            incomplete = false;
  std::string                     detail;
  std::array<Trend, metric_count> trends;
  uint64_t                        topology = 0, geometry = 0, timing_model = 0;
  bool                            virtual_timing = false;
  std::optional<double>           density_overflow;
};
struct Decision {
  std::vector<Event>              events;
  std::array<Trend, metric_count> trends;
  std::optional<double>           endpoint_jaccard;
  bool                            sampled = false, evaluated = false, dwelling = false;
};
// An observer initially: callers apply emitted actions only after proxy/force
// validation. tick() must be called at each global GP iteration; observations
// may be missing, but a supplied sample must obey the configured cadence.
// Phases 4/5 (optional resynthesis) are intentionally absent from this first
// controller; no resynthesis work or budget is silently admitted.
class Controller {
  Settings                             settings;
  std::array<History, metric_count>    histories;
  std::array<Trend, metric_count>      trends;
  std::optional<Sample>                last_sample;
  std::optional<std::set<std::string>> previous_endpoints;
  int                                  iteration = -1, dwell_until = 0, samples = 0;
  int                                  timing_started = -1, repair_started = -1, correction_at = -1;
  std::optional<double>                correction_overflow;
  bool memory_active = false, rudy_active = false, timing_active = false, repair_active = false, finished = false;
  bool timing_late_reported = false, repair_late_reported = false;
  bool correction_dirty = false;
  void reset_histories();

public:
  explicit Controller(Settings config = {});
  Decision tick(int global_iteration, std::optional<Sample> sample = std::nullopt);
  bool     done() const { return finished; }
  int      budget() const { return settings.budget; }
};
}  // namespace livehd::placement::timeclosure
