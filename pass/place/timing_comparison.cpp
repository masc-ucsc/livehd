// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "timing_comparison.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace livehd::placement::late {
namespace {
std::optional<double> value(const State& state, unsigned field) {
  return field == 0 ? state.arrival_ps : field == 1 ? state.required_ps : field == 2 ? state.slew_ps : state.load_ff;
}
void finite(std::optional<double> number) {
  if (number && !std::isfinite(*number)) {
    throw std::invalid_argument("nonfinite timing comparison state");
  }
}
std::optional<double> tau_b(std::vector<std::pair<double, double>> pairs) {
  const size_t n = pairs.size();
  if (n < 2) {
    return {};
  }
  std::sort(pairs.begin(), pairs.end());
  std::vector<double> ys;
  for (const auto& pair : pairs) {
    ys.push_back(pair.second);
  }
  std::sort(ys.begin(), ys.end());
  long double tied_y = 0, tied_x = 0;
  for (size_t i = 0; i < n;) {
    size_t j = i + 1;
    while (j < n && ys[j] == ys[i]) {
      ++j;
    }
    tied_y += static_cast<long double>(j - i) * (j - i - 1) / 2;
    i       = j;
  }
  ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
  std::vector<size_t> tree(ys.size() + 1);
  const auto          prefix = [&](size_t index) {
    size_t sum = 0;
    for (; index; index &= index - 1) {
      sum += tree[index];
    }
    return sum;
  };
  long double score = 0;
  size_t      seen  = 0;
  for (size_t i = 0; i < n;) {
    size_t j = i + 1;
    while (j < n && pairs[j].first == pairs[i].first) {
      ++j;
    }
    tied_x += static_cast<long double>(j - i) * (j - i - 1) / 2;
    for (size_t k = i; k < j; ++k) {
      const size_t rank  = std::lower_bound(ys.begin(), ys.end(), pairs[k].second) - ys.begin() + 1;
      score             += static_cast<long double>(prefix(rank - 1)) - (seen - prefix(rank));
    }
    for (size_t k = i; k < j; ++k) {
      size_t rank = std::lower_bound(ys.begin(), ys.end(), pairs[k].second) - ys.begin() + 1;
      for (; rank < tree.size(); rank += rank & (~rank + 1)) {
        ++tree[rank];
      }
      ++seen;
    }
    i = j;
  }
  const long double total       = static_cast<long double>(n) * (n - 1) / 2;
  const long double denominator = std::sqrt((total - tied_x) * (total - tied_y));
  return denominator > 0 ? std::optional<double>(score / denominator) : std::nullopt;
}
}  // namespace

Timing_comparison compare_timing(const Result& prediction, const Result& reference) {
  std::unordered_map<std::string_view, const Pin_result*> predicted;
  std::unordered_set<std::string_view>                    expected;
  predicted.reserve(prediction.pins.size());
  expected.reserve(reference.pins.size());
  std::vector<std::pair<const Pin_result*, const Pin_result*>> aligned;
  aligned.reserve(reference.pins.size());
  for (const auto& pin : prediction.pins) {
    if (!predicted.emplace(pin.name, &pin).second) {
      throw std::invalid_argument("duplicate predicted timing pin");
    }
  }
  for (const auto& pin : reference.pins) {
    if (!expected.emplace(pin.name).second) {
      throw std::invalid_argument("duplicate reference timing pin");
    }
    const auto found = predicted.find(pin.name);
    if (found == predicted.end()) {
      throw std::invalid_argument("timing comparison pin identities differ");
    }
    aligned.emplace_back(found->second, &pin);
  }
  if (predicted.size() != expected.size()) {
    throw std::invalid_argument("timing comparison pin census differs");
  }
  Timing_comparison result;
  if (prediction.characterized_boundary_policy != reference.characterized_boundary_policy
      || prediction.bounded_boundary_policy != reference.bounded_boundary_policy) {
    throw std::invalid_argument("timing comparison driver policies differ");
  }
  result.supported_coverage_pass = prediction.certified && reference.certified;
  std::vector<std::pair<double, double>> endpoints;
  for (unsigned field = 0; field < 4; ++field) {
    auto& error = result.fields[field];
    if (field < 3) {
      error.tolerance_failures = 0;
    }
    double sum = 0;
    for (const auto& [actual_pin, pin] : aligned) {
      const auto& name   = pin->name;
      const auto& actual = *actual_pin;
      for (unsigned t = 0; t < 2; ++t) {
        const auto a = value(actual.transitions[t], field), b = value(pin->transitions[t], field);
        finite(a);
        finite(b);
        if (field >= 2 && ((a && *a < 0) || (b && *b < 0))) {
          throw std::invalid_argument("negative comparison slew/load");
        }
        error.coverage_disagreements += a.has_value() != b.has_value();
        if (!a || !b) {
          continue;
        }
        ++error.compared;
        const double delta = std::abs(*a - *b);
        finite(std::optional<double>{delta});
        const double tolerance  = field == 2 ? std::max(1e-8, std::abs(*b) * .1) : std::max(5., std::abs(*b) * .05);
        sum                    += delta;
        finite(std::optional<double>{sum});
        if (delta > error.maximum) {
          error.maximum = delta;
          error.worst   = Timing_difference{name, t, *a, *b, delta, tolerance};
        }
        if (field < 3 && delta > tolerance) {
          ++*error.tolerance_failures;
          error.failure_examples.push_back({name, t, *a, *b, delta, tolerance});
          std::sort(error.failure_examples.begin(), error.failure_examples.end(), [](const auto& x, const auto& y) {
            const double a_ratio = x.error / x.tolerance, b_ratio = y.error / y.tolerance;
            if (a_ratio != b_ratio) {
              return a_ratio > b_ratio;
            }
            return std::pair{x.pin, x.transition} < std::pair{y.pin, y.transition};
          });
          if (error.failure_examples.size() > 8) {
            error.failure_examples.pop_back();
          }
        }
      }
      if (field != 0 || (!pin->endpoint && !actual.endpoint)) {
        continue;
      }
      if (pin->endpoint != actual.endpoint) {
        ++result.ranking.coverage_disagreements;
        continue;
      }
      for (unsigned t = 0; t < 2; ++t) {
        const auto& a          = actual.transitions[t];
        const auto& b          = pin->transitions[t];
        const bool  a_complete = a.arrival_ps && a.required_ps, b_complete = b.arrival_ps && b.required_ps;
        result.ranking.coverage_disagreements += a_complete != b_complete;
        if (a_complete && b_complete) {
          finite(a.required_ps);
          finite(b.required_ps);
          endpoints.emplace_back(*b.required_ps - *b.arrival_ps, *a.required_ps - *a.arrival_ps);
        }
      }
    }
    if (error.compared) {
      error.mean = sum / error.compared;
    }
  }
  auto& rank              = result.ranking;
  rank.compared_endpoints = endpoints.size();
  std::map<std::string, double> p_clocks, r_clocks;
  std::set<double>              periods;
  for (const auto& [name, clock] : prediction.clocks) {
    if (!std::isfinite(clock.period_ps) || clock.period_ps <= 0) {
      throw std::invalid_argument("invalid comparison clock period");
    }
    if (clock.timed_endpoints) {
      p_clocks.emplace(name, clock.period_ps);
    }
  }
  for (const auto& [name, clock] : reference.clocks) {
    if (!std::isfinite(clock.period_ps) || clock.period_ps <= 0) {
      throw std::invalid_argument("invalid comparison clock period");
    }
    if (clock.timed_endpoints) {
      r_clocks.emplace(name, clock.period_ps);
      periods.insert(clock.period_ps);
    }
  }
  if (p_clocks != r_clocks || periods.size() != 1) {
    rank.unavailable = "ranking requires one common active clock period";
  } else if (endpoints.empty()) {
    rank.unavailable = "no common timed endpoint transitions";
  } else {
    const double period           = *periods.begin();
    const auto   reference_worst  = std::min_element(endpoints.begin(), endpoints.end(), [](const auto& a, const auto& b) {
                                   return a.first < b.first;
                                 })->first;
    const auto   prediction_worst = std::min_element(endpoints.begin(), endpoints.end(), [](const auto& a, const auto& b) {
                                    return a.second < b.second;
                                  })->second;
    if (!std::isfinite(period) || period <= 0 || period - reference_worst <= 0 || period - prediction_worst <= 0) {
      rank.unavailable = "nonpositive observed period window";
    } else {
      rank.period_ps = period;
      std::vector<std::pair<double, double>> critical;
      size_t                                 intersection = 0;
      for (const auto& pair : endpoints) {
        const bool a              = pair.first <= reference_worst + .1 * (period - reference_worst);
        const bool b              = pair.second <= prediction_worst + .1 * (period - prediction_worst);
        rank.reference_critical  += a;
        rank.prediction_critical += b;
        intersection             += a && b;
        if (a || b) {
          critical.push_back(pair);
        }
      }
      rank.union_size = critical.size();
      rank.jaccard    = double(intersection) / critical.size();
      rank.tau_b      = tau_b(std::move(critical));
      if (!rank.tau_b) {
        rank.unavailable = "critical ranking has insufficient distinct pairs";
      }
    }
  }
  if (rank.coverage_disagreements) {
    rank.jaccard.reset();
    rank.tau_b.reset();
    rank.unavailable = "endpoint coverage differs";
  }
  result.numerical_gates_pass = true;
  for (unsigned field = 0; field < 3; ++field) {
    const auto& error            = result.fields[field];
    result.numerical_gates_pass &= error.compared > 0 && !error.coverage_disagreements && *error.tolerance_failures == 0;
  }
  result.numerical_gates_pass &= result.fields[3].coverage_disagreements == 0;
  result.ranking_gates_pass
      = !rank.coverage_disagreements && rank.jaccard && *rank.jaccard >= .9 && rank.tau_b && *rank.tau_b >= .9;
  result.comparison_gates_pass = result.numerical_gates_pass && result.ranking_gates_pass;
  return result;
}
}  // namespace livehd::placement::late
