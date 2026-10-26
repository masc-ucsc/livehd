// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "timeclosure.hpp"

#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

#include "gtest/gtest.h"

namespace tc = livehd::placement::timeclosure;
namespace {
tc::Sample sample(int at, double value = 1) {
  tc::Sample result;
  result.iteration        = at;
  result.values           = {value, -value, 100 * value, .1 * value};
  result.density_overflow = .5;
  result.endpoints        = std::set<std::string>{"d:rise", "q:fall"};
  return result;
}
std::vector<tc::Event> run(tc::Controller& controller, const std::function<tc::Sample(int)>& make_sample) {
  std::vector<tc::Event> events;
  for (int at = 0; at <= controller.budget() && !controller.done(); ++at) {
    const auto decision = controller.tick(at, at % 10 == 0 ? std::optional{make_sample(at)} : std::nullopt);
    events.insert(events.end(), decision.events.begin(), decision.events.end());
  }
  return events;
}
const tc::Event* find(const std::vector<tc::Event>& events, tc::Gate gate, tc::Reason reason) {
  for (const auto& event : events) {
    if (event.gate == gate && event.reason == reason) {
      return &event;
    }
  }
  return nullptr;
}
}  // namespace

TEST(Timeclosure, ZeroRangeRequiresTwoDistinctEvaluations) {
  tc::History  history;
  tc::Settings config;
  for (int i = 0; i < 5; ++i) {
    history.append(i * 10, 0);
  }
  auto trend = history.evaluate(config);
  EXPECT_TRUE(trend.ready);
  EXPECT_TRUE(trend.pass);
  EXPECT_FALSE(trend.stable);
  EXPECT_FALSE(history.evaluate(config).stable);
  for (int i = 5; i < 8; ++i) {
    history.append(i * 10, 0);
  }
  trend = history.evaluate(config);
  EXPECT_TRUE(trend.stable);
  EXPECT_EQ(trend.noise, 0);
  EXPECT_EQ(trend.range, 0);
}
TEST(Timeclosure, PersistentSlopeNeverPlateausAndUnitsDoNotMatter) {
  for (double scale : {1e-6, 1., 1e6}) {
    tc::History  history;
    tc::Settings config;
    for (int i = 0; i < 80; ++i) {
      history.append(i * 10, (100 - i) * scale);
      EXPECT_FALSE(history.evaluate(config).pass);
    }
  }
}
TEST(Timeclosure, TurningPointDoesNotPassEvenWithZeroOverallSlope) {
  tc::History history;
  for (int i = 0; auto value : {0., 2., 4., 2., 0.}) {
    history.append(i++ * 10, value);
  }
  const auto trend = history.evaluate({});
  EXPECT_EQ(trend.slope, 0);
  EXPECT_TRUE(trend.turning);
  EXPECT_FALSE(trend.pass);
}
TEST(Timeclosure, MedianSlopeResistsSingleOutlier) {
  tc::History history;
  for (int i = 0; auto value : {1., 1., 1000., 1., 1.}) {
    history.append(i++ * 10, value);
  }
  const auto trend = history.evaluate({});
  EXPECT_EQ(trend.slope, 0);
  EXPECT_EQ(trend.noise, 0);
  // The overlapping half windows still flag a spike as a turning point.
  EXPECT_TRUE(trend.turning);
  EXPECT_FALSE(trend.pass);
}
TEST(Timeclosure, MissingDataClearsPreviousPassAndRange) {
  tc::History history;
  for (int i = 0; i < 8; ++i) {
    history.append(i * 10, 100);
    history.evaluate({});
  }
  ASSERT_TRUE(history.evaluate({}).stable);
  history.append(80, std::nullopt);
  for (int i = 9; i < 14; ++i) {
    history.append(i * 10, 1);
  }
  const auto trend = history.evaluate({});
  EXPECT_FALSE(trend.stable);
  EXPECT_EQ(trend.range, 0);
}
TEST(Timeclosure, EndpointOverlapHasDefinedEmptyCases) {
  EXPECT_EQ(tc::jaccard({}, {}), 1);
  EXPECT_EQ(tc::jaccard({"a"}, {}), 0);
  EXPECT_EQ(tc::jaccard({"a", "b"}, {"b", "c"}), 1. / 3);
}
TEST(Timeclosure, StableTraceLatchesIndependentPreparationGatesAndFinishesEarly) {
  tc::Settings config;
  config.memory = true;
  tc::Controller controller(config);
  const auto     events = run(controller, [](int at) { return sample(at); });
  const auto*    memory = find(events, tc::Gate::memory, tc::Reason::stable);
  const auto*    rudy   = find(events, tc::Gate::rudy, tc::Reason::stable);
  ASSERT_NE(memory, nullptr);
  ASSERT_NE(rudy, nullptr);
  EXPECT_EQ(memory->iteration, 70);
  EXPECT_EQ(memory->iteration, rudy->iteration);
  const auto* timing = find(events, tc::Gate::timing, tc::Reason::stable);
  const auto* repair = find(events, tc::Gate::repair, tc::Reason::stable);
  const auto* finish = find(events, tc::Gate::finish, tc::Reason::stable);
  ASSERT_NE(timing, nullptr);
  ASSERT_NE(repair, nullptr);
  ASSERT_NE(finish, nullptr);
  EXPECT_GE(timing->iteration, memory->iteration + config.memory_growth + config.memory_settle + 70);
  EXPECT_GE(repair->iteration, timing->iteration + config.timing_response + 70);
  EXPECT_GE(finish->iteration, repair->iteration + config.repair_dwell + 70);
  EXPECT_LT(finish->iteration, config.budget);
  EXPECT_FALSE(finish->incomplete);
}
TEST(Timeclosure, SlopingTraceUsesDeadlinesAndHardBudget) {
  tc::Controller controller;
  const auto     events = run(controller, [](int at) { return sample(at, 1000 - at); });
  ASSERT_NE(find(events, tc::Gate::rudy, tc::Reason::deadline), nullptr);
  ASSERT_NE(find(events, tc::Gate::timing, tc::Reason::deadline), nullptr);
  ASSERT_NE(find(events, tc::Gate::repair, tc::Reason::deadline), nullptr);
  EXPECT_EQ(find(events, tc::Gate::rudy, tc::Reason::deadline)->iteration, 200);
  EXPECT_EQ(find(events, tc::Gate::timing, tc::Reason::deadline)->iteration, 360);
  EXPECT_EQ(find(events, tc::Gate::repair, tc::Reason::deadline)->iteration, 600);
  const auto* finish = find(events, tc::Gate::finish, tc::Reason::budget);
  ASSERT_NE(finish, nullptr);
  EXPECT_EQ(finish->iteration, 800);
  EXPECT_TRUE(finish->incomplete);
}
TEST(Timeclosure, ShortBudgetReportsPrerequisitesRatherThanSkippingMemoryDwell) {
  tc::Settings config;
  config.budget = 80;
  config.memory = true;
  tc::Controller controller(config);
  const auto     events = run(controller, [](int at) { return sample(at); });
  EXPECT_NE(find(events, tc::Gate::timing, tc::Reason::precondition), nullptr);
  EXPECT_EQ(find(events, tc::Gate::timing, tc::Reason::deadline), nullptr);
  EXPECT_EQ(find(events, tc::Gate::repair, tc::Reason::deadline), nullptr);
  const auto* finish = find(events, tc::Gate::finish, tc::Reason::budget);
  ASSERT_NE(finish, nullptr);
  EXPECT_EQ(finish->iteration, 80);
  EXPECT_TRUE(finish->incomplete);
}
TEST(Timeclosure, EpochChangePreservesLatchesAndBudgetButResetsEvidence) {
  tc::Controller controller;
  bool           reset = false;
  int            rudys = 0;
  for (int at = 0; at <= 800 && !controller.done(); ++at) {
    auto observation           = sample(at, 1000 - at);
    observation.topology       = at >= 500 ? 1 : 0;
    observation.virtual_timing = at >= 400;
    const auto decision        = controller.tick(at, at % 10 == 0 ? std::optional{observation} : std::nullopt);
    for (const auto& event : decision.events) {
      reset |= event.gate == tc::Gate::reset && event.reason == tc::Reason::epoch;
      rudys += event.gate == tc::Gate::rudy;
    }
    if (at == 500) {
      EXPECT_FALSE(decision.evaluated);
      EXPECT_FALSE(decision.trends[0].ready);
    }
  }
  EXPECT_TRUE(reset);
  EXPECT_EQ(rudys, 1);
  EXPECT_TRUE(controller.done());
}
TEST(Timeclosure, TinyBudgetEndsWithoutPretendingLongDwellsCompleted) {
  tc::Settings config;
  config.budget = 12;
  config.memory = true;
  tc::Controller controller(config);
  const auto     events = run(controller, [](int at) { return sample(at); });
  EXPECT_EQ(find(events, tc::Gate::timing, tc::Reason::deadline), nullptr);
  EXPECT_EQ(find(events, tc::Gate::repair, tc::Reason::deadline), nullptr);
  const auto* finish = find(events, tc::Gate::finish, tc::Reason::budget);
  ASSERT_NE(finish, nullptr);
  EXPECT_EQ(finish->iteration, 12);
  EXPECT_TRUE(finish->incomplete);
}
TEST(Timeclosure, DirtyCorrectionDuringDwellPersistsUntilTheNextCheckpoint) {
  tc::Controller controller;
  bool           refreshed = false;
  for (int at = 0; at <= 400; ++at) {
    auto observation     = sample(at, 1000 - at);
    observation.geometry = at >= 370 ? 1 : 0;
    const auto decision  = controller.tick(at, at % 10 == 0 ? std::optional{observation} : std::nullopt);
    for (const auto& event : decision.events) {
      if (event.gate == tc::Gate::correction && event.reason == tc::Reason::epoch && at > 360) {
        EXPECT_EQ(at, 380);
        EXPECT_EQ(event.geometry, 1);
        refreshed = true;
      }
    }
  }
  EXPECT_TRUE(refreshed);
}
TEST(Timeclosure, MissingWindowMetricCannotClaimStableGates) {
  tc::Controller controller;
  const auto     events = run(controller, [](int at) {
    auto result = sample(at);
    result.values[static_cast<size_t>(tc::Metric::window_tns)].reset();
    return result;
  });
  EXPECT_EQ(find(events, tc::Gate::rudy, tc::Reason::stable), nullptr);
  EXPECT_EQ(find(events, tc::Gate::repair, tc::Reason::stable), nullptr);
  EXPECT_NE(find(events, tc::Gate::repair, tc::Reason::deadline), nullptr);
}
TEST(Timeclosure, SkippedObservationsCannotManufacturePlateau) {
  tc::Controller controller;
  for (int at = 0; at <= 70; at += 10) {
    const auto decision = controller.tick(at, at == 50 ? std::nullopt : std::optional{sample(at)});
    EXPECT_FALSE(decision.trends[0].stable);
    EXPECT_EQ(find(decision.events, tc::Gate::rudy, tc::Reason::stable), nullptr);
  }
}
TEST(Timeclosure, CalibrationUsesDensityAndMaximumAgeNotRouteOverflow) {
  tc::Controller         controller;
  std::vector<tc::Event> events;
  for (int at = 0; at <= 520; ++at) {
    auto observation                                                 = sample(at, 1000 - at);
    observation.values[static_cast<size_t>(tc::Metric::opaque_rudy)] = at >= 390 ? .1 : .9;
    observation.density_overflow                                     = at >= 430 ? .4 : .5;
    const auto decision = controller.tick(at, at % 10 == 0 ? std::optional{observation} : std::nullopt);
    events.insert(events.end(), decision.events.begin(), decision.events.end());
  }
  const auto* age      = find(events, tc::Gate::correction, tc::Reason::age);
  const auto* overflow = find(events, tc::Gate::correction, tc::Reason::overflow);
  ASSERT_NE(age, nullptr);
  ASSERT_NE(overflow, nullptr);
  EXPECT_EQ(age->iteration, 410);
  EXPECT_EQ(overflow->iteration, 430);
}
TEST(Timeclosure, InputValidationRejectsInvalidCadenceAndNonfiniteData) {
  tc::Settings config;
  config.horizon = 0;
  EXPECT_THROW(tc::Controller{config}, std::invalid_argument);
  tc::Controller controller;
  EXPECT_THROW(controller.tick(1, sample(1)), std::invalid_argument);
  auto invalid      = sample(0);
  invalid.values[0] = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(controller.tick(0, invalid), std::invalid_argument);
  controller.tick(0, sample(0));
  EXPECT_THROW(controller.tick(0), std::invalid_argument);
  EXPECT_THROW(controller.tick(801), std::invalid_argument);
}
