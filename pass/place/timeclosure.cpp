// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "timeclosure.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace livehd::placement::timeclosure {
namespace {
double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const auto mid = values.size() / 2;
  return values.size() % 2 ? values[mid] : (values[mid - 1] + values[mid]) / 2;
}
using Point = std::pair<int, double>;
double slope(const std::vector<Point>& points, size_t begin, size_t end) {
  std::vector<double> slopes;
  for (size_t i = begin; i < end; ++i) {
    for (size_t j = i + 1; j < end; ++j) {
      slopes.push_back((points[j].second - points[i].second) / (points[j].first - points[i].first));
    }
  }
  return median(std::move(slopes));
}
constexpr size_t index(Metric metric) { return static_cast<size_t>(metric); }
}  // namespace

void Settings::validate() const {
  if (budget < 1 || sample_interval < 1 || window < 5 || memory_growth < 0 || memory_settle < 0 || rudy_dwell < 0
      || timing_response < 1 || repair_dwell < 1 || correction_max_age < 1) {
    throw std::invalid_argument("invalid timeclosure budget, cadence, window or dwell");
  }
  if (!std::isfinite(horizon) || horizon <= 0 || !std::isfinite(range_fraction) || range_fraction < 0 || range_fraction > 1
      || !std::isfinite(noise_multiplier) || noise_multiplier < 0 || !std::isfinite(endpoint_overlap) || endpoint_overlap < 0
      || endpoint_overlap > 1 || !std::isfinite(correction_overflow_drop) || correction_overflow_drop <= 0) {
    throw std::invalid_argument("invalid timeclosure stability or calibration setting");
  }
}
void History::reset() {
  points.clear();
  previous_pass  = false;
  evaluated_size = 0;
  latest         = {};
}
void History::append(int at, std::optional<double> value) {
  if (!points.empty() && at <= points.back().first) {
    throw std::invalid_argument("timeclosure history iterations must increase");
  }
  if (!value) {
    reset();
    return;
  }
  if (!std::isfinite(*value)) {
    throw std::invalid_argument("timeclosure metric must be finite or missing");
  }
  points.emplace_back(at, *value);
}
Trend History::evaluate(const Settings& config) {
  const size_t n = static_cast<size_t>(config.window), stride = (n + 1) / 2;
  if (points.size() < n || (evaluated_size && points.size() - evaluated_size < stride)) {
    return latest;
  }
  Trend result;
  result.ready        = true;
  result.samples      = static_cast<int>(points.size());
  result.value        = points.back().second;
  const auto [lo, hi] = std::minmax_element(points.begin(), points.end(), [](auto a, auto b) { return a.second < b.second; });
  result.range        = hi->second - lo->second;
  const auto begin = points.size() - n, end = points.size();
  result.slope = slope(points, begin, end);
  std::vector<double> residuals;
  for (size_t i = begin; i < end; ++i) {
    residuals.push_back(points[i].second - result.slope * (points[i].first - points[begin].first));
  }
  const auto intercept = median(residuals);
  for (auto& residual : residuals) {
    residual = std::abs(residual - intercept);
  }
  result.noise     = 1.4826 * median(std::move(residuals));
  // Odd N uses overlapping halves, e.g. [0,1,2] and [2,3,4].
  const auto half  = (n + 1) / 2;
  const auto first = slope(points, begin, begin + half), second = slope(points, end - half, end);
  const auto significant = config.noise_multiplier * result.noise / config.horizon;
  result.turning         = first * second < 0 && std::abs(first) > significant && std::abs(second) > significant;
  result.pass            = !result.turning
                && std::abs(result.slope) * config.horizon
                       <= std::max(config.range_fraction * result.range, config.noise_multiplier * result.noise);
  result.stable  = result.pass && previous_pass;
  previous_pass  = result.pass;
  evaluated_size = points.size();
  latest         = result;
  return latest;
}
double jaccard(const std::set<std::string>& a, const std::set<std::string>& b) {
  if (a.empty() && b.empty()) {
    return 1;
  }
  size_t shared = 0;
  auto   x = a.begin(), y = b.begin();
  while (x != a.end() && y != b.end()) {
    if (*x < *y) {
      ++x;
    } else if (*y < *x) {
      ++y;
    } else {
      ++shared;
      ++x;
      ++y;
    }
  }
  return static_cast<double>(shared) / (a.size() + b.size() - shared);
}
const char* name(Metric metric) {
  switch (metric) {
    case Metric::period     : return "period";
    case Metric::window_tns : return "window_tns";
    case Metric::hpwl       : return "hpwl";
    case Metric::opaque_rudy: return "opaque_rudy";
    case Metric::count      : break;
  }
  throw std::invalid_argument("unknown timeclosure metric");
}
const char* name(Gate gate) {
  switch (gate) {
    case Gate::memory    : return "memory";
    case Gate::rudy      : return "rudy";
    case Gate::timing    : return "timing";
    case Gate::repair    : return "repair";
    case Gate::finish    : return "finish";
    case Gate::correction: return "correction";
    case Gate::reset     : return "reset";
  }
  throw std::invalid_argument("unknown timeclosure gate");
}
const char* name(Reason reason) {
  switch (reason) {
    case Reason::stable      : return "stable";
    case Reason::deadline    : return "deadline";
    case Reason::budget      : return "budget";
    case Reason::precondition: return "precondition";
    case Reason::age         : return "age";
    case Reason::overflow    : return "overflow";
    case Reason::epoch       : return "epoch";
    case Reason::complete    : return "complete";
  }
  throw std::invalid_argument("unknown timeclosure reason");
}
Controller::Controller(Settings config) : settings(config) { settings.validate(); }
void Controller::reset_histories() {
  for (auto& history : histories) {
    history.reset();
  }
  trends = {};
  previous_endpoints.reset();
  samples = 0;
}
Decision Controller::tick(int at, std::optional<Sample> sample) {
  if (at < 0 || at <= iteration || at > settings.budget || finished) {
    throw std::invalid_argument("timeclosure iterations must increase within the active hard budget");
  }
  if (sample && (sample->iteration != at || at % settings.sample_interval)) {
    throw std::invalid_argument("timeclosure sample does not match global iteration/cadence");
  }
  if (sample) {
    for (size_t i = 0; i < metric_count; ++i) {
      if (sample->values[i] && (!std::isfinite(*sample->values[i]) || (i != index(Metric::window_tns) && *sample->values[i] < 0))) {
        throw std::invalid_argument("invalid timeclosure sample metric");
      }
    }
    if (sample->density_overflow && (!std::isfinite(*sample->density_overflow) || *sample->density_overflow < 0)) {
      throw std::invalid_argument("invalid timeclosure density overflow");
    }
  }
  const int previous_iteration = iteration;
  iteration                    = at;
  Decision result;
  auto     emit = [&](Gate gate, Reason reason, int dwell, bool incomplete = false, std::string detail = {}) {
    result.events.push_back({gate, reason, at, dwell, settings.budget - at, incomplete, std::move(detail), trends});
    const auto& current = sample ? sample : last_sample;
    if (current) {
      auto& event            = result.events.back();
      event.topology         = current->topology;
      event.geometry         = current->geometry;
      event.timing_model     = current->timing_model;
      event.virtual_timing   = current->virtual_timing;
      event.density_overflow = current->density_overflow;
    }
  };
  if (dwell_until > 0 && at >= dwell_until) {
    reset_histories();
    dwell_until = 0;
    emit(Gate::reset, Reason::complete, 0, false, "dwell ended");
  }
  const bool epoch
      = sample && last_sample
        && (sample->topology != last_sample->topology || sample->geometry != last_sample->geometry
            || sample->timing_model != last_sample->timing_model || sample->virtual_timing != last_sample->virtual_timing);
  if (epoch) {
    correction_dirty = true;
    reset_histories();
    emit(Gate::reset, Reason::epoch, dwell_until, false, "timing/topology/geometry epoch changed");
  }
  // Missing scheduled observations break consecutive stability evidence.
  const int64_t first_scheduled = (int64_t{previous_iteration} / settings.sample_interval + 1) * settings.sample_interval;
  if ((at > first_scheduled && first_scheduled > previous_iteration) || (!sample && at % settings.sample_interval == 0)) {
    reset_histories();
  }
  if (sample) {
    result.sampled = true;
    last_sample    = *sample;
    for (size_t i = 0; i < metric_count; ++i) {
      histories[i].append(at, sample->values[i]);
    }
    ++samples;
    const int stride = (settings.window + 1) / 2;
    if (!dwell_until && samples >= settings.window && (samples - settings.window) % stride == 0) {
      result.evaluated = true;
      for (size_t i = 0; i < metric_count; ++i) {
        trends[i] = histories[i].evaluate(settings);
      }
      if (sample->endpoints && previous_endpoints) {
        result.endpoint_jaccard = jaccard(*sample->endpoints, *previous_endpoints);
      }
      previous_endpoints = sample->endpoints;
    }
  }
  auto stable = [&](std::initializer_list<Metric> metrics) {
    return !dwell_until && result.evaluated
           && std::all_of(metrics.begin(), metrics.end(), [&](auto m) { return trends[index(m)].stable; });
  };
  auto      dwell_to = [&](int64_t length) { return at + static_cast<int>(std::min<int64_t>(length, settings.budget - at)); };
  const int prepare_deadline = static_cast<int>(std::ceil(.25 * settings.budget));
  const int timing_deadline  = static_cast<int>(std::ceil(.45 * settings.budget));
  const int repair_deadline  = settings.budget - static_cast<int>(std::ceil(.25 * settings.budget));
  if (at == settings.budget) {
    finished = true;
    emit(Gate::finish,
         Reason::budget,
         dwell_until,
         true,
         !repair_active ? "repair prerequisites/budget unmet" : "hard budget exhausted before stable completion");
  } else {
    const bool preparation_stable = stable({Metric::period, Metric::window_tns, Metric::hpwl});
    if (settings.memory && !memory_active && (preparation_stable || at >= prepare_deadline)) {
      memory_active = true;
      dwell_until   = std::max(dwell_until, dwell_to(int64_t{settings.memory_growth} + settings.memory_settle));
      emit(Gate::memory, preparation_stable ? Reason::stable : Reason::deadline, dwell_until);
    }
    // Independent latches: simultaneous memory/RUDY actions share the longer dwell.
    if (!rudy_active && (preparation_stable || at >= prepare_deadline)) {
      rudy_active = true;
      dwell_until = std::max(dwell_until, dwell_to(settings.rudy_dwell));
      emit(Gate::rudy, preparation_stable ? Reason::stable : Reason::deadline, dwell_until);
    }
    if (!timing_active) {
      const bool settled       = stable({Metric::hpwl, Metric::opaque_rudy});
      const bool prerequisites = rudy_active && (!settings.memory || memory_active) && !dwell_until;
      if (prerequisites && (settled || at >= timing_deadline) && settings.budget - at > settings.timing_response) {
        timing_active       = true;
        timing_started      = at;
        correction_at       = at;
        correction_overflow = last_sample ? last_sample->density_overflow : std::nullopt;
        correction_dirty    = false;
        dwell_until         = dwell_to(settings.timing_response);
        emit(Gate::timing, settled ? Reason::stable : Reason::deadline, dwell_until);
        emit(Gate::correction, Reason::epoch, dwell_until, false, "first timing checkpoint");
      } else if (at >= timing_deadline && !timing_late_reported) {
        timing_late_reported = true;
        emit(Gate::timing, Reason::precondition, dwell_until, true, "RUDY/memory dwell or timing-response budget unmet");
      }
    }
    if (timing_active && !repair_active) {
      const bool settled       = stable({Metric::period, Metric::window_tns, Metric::hpwl, Metric::opaque_rudy});
      const bool prerequisites = !dwell_until && at - timing_started >= settings.timing_response;
      if (prerequisites && (settled || at >= repair_deadline) && settings.budget - at >= settings.repair_dwell) {
        repair_active  = true;
        repair_started = at;
        dwell_until    = dwell_to(settings.repair_dwell);
        emit(Gate::repair, settled ? Reason::stable : Reason::deadline, dwell_until);
      } else if (at >= repair_deadline && !repair_late_reported) {
        repair_late_reported = true;
        emit(Gate::repair, Reason::precondition, dwell_until, true, "timing response or repair-dwell budget unmet");
      }
    }
    if (repair_active && at - repair_started >= settings.repair_dwell
        && stable({Metric::period, Metric::hpwl, Metric::opaque_rudy})) {
      finished = true;
      emit(Gate::finish, Reason::stable, 0);
    }
    if (timing_active && sample && !dwell_until && !finished && correction_at != at) {
      const bool drop = sample->density_overflow && correction_overflow
                        && *correction_overflow - *sample->density_overflow >= settings.correction_overflow_drop - 1e-12;
      if (correction_dirty || drop || at - correction_at >= settings.correction_max_age) {
        emit(Gate::correction, correction_dirty ? Reason::epoch : drop ? Reason::overflow : Reason::age, 0);
        correction_dirty    = false;
        correction_at       = at;
        correction_overflow = sample->density_overflow;
      }
    }
  }
  result.trends   = trends;
  result.dwelling = dwell_until > at;
  return result;
}
}  // namespace livehd::placement::timeclosure
