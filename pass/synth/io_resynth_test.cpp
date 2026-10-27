// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "io_resynth.hpp"

#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

namespace livehd::synth {
TEST(IoResynth, TriggerNeedsCriticalPathAndTightening) {
  EXPECT_FALSE(io_trigger(100, 80, 1000, 5));
  EXPECT_FALSE(io_trigger(-100, -80, 1000, 5));
  EXPECT_FALSE(io_trigger(-100, 50, 1000, 5));
  EXPECT_FALSE(io_trigger(-100, 500, 1000, 0));
  EXPECT_TRUE(io_trigger(50, 51, 1000, 5));
  EXPECT_TRUE(io_trigger(-100, 80, 1000, 5));
}
TEST(IoResynth, StageIdentityAndColdMapRules) {
  const auto directory = std::string(std::getenv("TEST_TMPDIR")) + "/io_store";
  std::filesystem::remove_all(directory);
  Io_environment env;
  env.inputs["a[0]"]         = 120;
  env.required["y[0]"]       = 800;
  env.wire.capacitance_ff_um = .2;
  env.wire.length_um         = {
      {1, 10},
      {3, 40}
  };
  {
    Io_store store(directory, 1000);
    EXPECT_TRUE(store.record("c1", "body1", "fp", env));
    EXPECT_TRUE(store.record("c1", "body1", "final", env));
    env.inputs["a[0]"] = 300;
    EXPECT_FALSE(store.record("c1", "body1", "macro", env));
    EXPECT_EQ(store.rows.at("c1").placed.inputs.at("a[0]"), 120);
    store.used("c1", "body1", store.rows.at("c1").placed);
    store.save();
  }
  {
    Io_store next(directory, 1000);
    EXPECT_EQ(next.rows.at("c1").used.inputs.at("a[0]"), 120);
    EXPECT_TRUE(next.record("c1", "body1", "fp", env));
    EXPECT_EQ(next.rows.at("c1").placed.inputs.at("a[0]"), 300);
    EXPECT_TRUE(next.record("c2", "body2", "final", env));
    next.invalidate("c1");
    EXPECT_FALSE(next.rows.contains("c1"));
    EXPECT_TRUE(next.rows.contains("c2"));
    EXPECT_EQ(next.wire("c1").length_um.at(3), 40);
    next.save();
  }
  Io_store last(directory, 1000);
  EXPECT_FALSE(last.rows.contains("c1"));
  EXPECT_TRUE(last.rows.contains("c2"));
  EXPECT_TRUE(last.record("c2", "body3", "macro", env));
  EXPECT_TRUE(last.rows.at("c2").used.inputs.empty());
  last.used("c2", "body4", env);
  EXPECT_TRUE(last.rows.at("c2").stage.empty());
  EXPECT_TRUE(last.rows.at("c2").placed.inputs.empty());
}
TEST(IoResynth, TimerChangeInvalidatesMeasuredTimingButPreservesMappingConstraintsAndWires) {
  const auto directory = std::string(std::getenv("TEST_TMPDIR")) + "/io_timer_identity";
  std::filesystem::remove_all(directory);
  Io_environment env;
  env.inputs["a[0]"]         = 120;
  env.required["y[0]"]       = 800;
  env.wire.capacitance_ff_um = .2;
  env.wire.length_um[1]      = 10;
  {
    Io_store old(directory, 1000, true, "timer-A");
    old.record("c1", "body", "final", env);
    old.used("c1", "body", env);
    old.save();
  }
  {
    Io_store same(directory, 1000, true, "timer-A");
    EXPECT_EQ(same.rows.at("c1").stage, "final");
  }
  {
    Io_store    next(directory, 1000, true, "timer-B");
    const auto& row = next.rows.at("c1");
    EXPECT_EQ(row.used.inputs.at("a[0]"), 120);
    EXPECT_TRUE(row.placed.inputs.empty());
    EXPECT_TRUE(row.stage.empty());
    EXPECT_EQ(next.wire("c1").length_um.at(1), 10);
    env.inputs["a[0]"] = 50;
    EXPECT_TRUE(next.record("c1", "body", "fp", env));
    next.save();
  }
  Io_store warm(directory, 1000, true, "timer-B");
  EXPECT_EQ(warm.rows.at("c1").placed.inputs.at("a[0]"), 50);
}
TEST(IoResynth, EnvironmentIncludesWireLoadAndPortIdentity) {
  Io_environment a, b;
  a.inputs["a[0]"] = 20;
  b.inputs["a[1]"] = 20;
  EXPECT_NE(environment_identity(a), environment_identity(b));
  b = a;
  EXPECT_EQ(environment_identity(a), environment_identity(b));
  b.wire.capacitance_ff_um = 1;
  b.wire.length_um[1]      = 10;
  EXPECT_NE(environment_identity(a), environment_identity(b));
}
TEST(IoResynth, FailedTargetsSurvivePlacementAndReloadButNotNewBodies) {
  const auto directory = std::string(std::getenv("TEST_TMPDIR")) + "/io_failed_target";
  std::filesystem::remove_all(directory);
  {
    Io_store store(directory, 1000, true, "timer-A");
    store.failed_target("c1", "body1", 400);
    store.failed_target("c1", "body1", 300);
    EXPECT_TRUE(store.target_failed("c1", "body1", 400));
    EXPECT_TRUE(store.target_failed("c1", "body1", 200));
    EXPECT_FALSE(store.target_failed("c1", "body1", 500));
    EXPECT_FALSE(store.target_failed("c2", "body1", 400));
    EXPECT_FALSE(store.target_failed("c1", "body2", 400));
    store.used("c1", "body1", {});
    store.record("c1", "body1", "final", {});
    store.save();
  }
  Io_store warm(directory, 1000, true, "timer-A");
  EXPECT_TRUE(warm.target_failed("c1", "body1", 400));
  warm.record("c1", "body1", "fp", {});
  EXPECT_TRUE(warm.target_failed("c1", "body1", 400));
  warm.refined("c1", "resized-body1");
  EXPECT_TRUE(warm.target_failed("c1", "resized-body1", 400));
  EXPECT_TRUE(warm.rows.at("c1").stage.empty());
  warm.used("c1", "body2", {});
  EXPECT_FALSE(warm.target_failed("c1", "body2", 400));
  warm.failed_target("c1", "body2", 400);
  warm.invalidate("c1");
  EXPECT_FALSE(warm.target_failed("c1", "body2", 400));
  Io_store new_timer(directory, 1000, true, "timer-B");
  EXPECT_FALSE(new_timer.target_failed("c1", "body1", 400));
}
TEST(IoResynth, DisabledPersistenceNeitherReadsNorWrites) {
  const auto directory = std::string(std::getenv("TEST_TMPDIR")) + "/io_disabled";
  std::filesystem::create_directories(directory);
  const auto path = directory + "/io_timing.json";
  {
    std::ofstream out(path);
    out << "corrupt";
  }
  Io_store transient(directory, 1000, false);
  EXPECT_TRUE(transient.rows.empty());
  Io_environment env;
  env.inputs["a[0]"] = 100;
  transient.record("c", "body", "final", env);
  transient.save();
  std::ifstream input(path);
  std::string   contents;
  input >> contents;
  EXPECT_EQ(contents, "corrupt");
  EXPECT_FALSE(std::filesystem::exists(directory + "/wire_load.json"));
}
TEST(IoResynth, TargetMarginAbsorbsSmallTimingChangesAcrossRuns) {
  const auto directory = std::string(std::getenv("TEST_TMPDIR")) + "/io_target_margin";
  std::filesystem::remove_all(directory);
  Io_environment env;
  env.inputs["a[0]"]   = 200;
  env.required["y[0]"] = 800;
  {
    Io_store store(directory, 1000, true, "timer-A");
    store.covered_target("c1", "body", 1000, 900, env);
    store.used("c1", "body", env);
    store.record("c1", "body", "final", env);
    store.save();
  }
  Io_store warm(directory, 1000, true, "timer-A");
  EXPECT_TRUE(warm.target_covered("c1", "body", 1000, env));
  auto shifted              = env;
  shifted.inputs["a[0]"]   += 20;
  shifted.required["y[0]"] -= 20;
  EXPECT_TRUE(warm.target_covered("c1", "body", 1000, shifted));
  shifted.inputs["a[0]"] += 70;
  EXPECT_FALSE(warm.target_covered("c1", "body", 1000, shifted));
  shifted                   = env;
  shifted.required["y[0]"] -= 20;
  EXPECT_TRUE(warm.target_covered("c1", "body", 980, shifted));
  shifted.required["y[0]"] -= 90;
  EXPECT_FALSE(warm.target_covered("c1", "body", 890, shifted));
  EXPECT_FALSE(warm.target_covered("c1", "new-body", 1000, env));
  warm.refined("c1", "resized-body");
  EXPECT_TRUE(warm.target_covered("c1", "resized-body", 1000, env));
  Io_store changed(directory, 1000, true, "timer-B");
  EXPECT_FALSE(changed.target_covered("c1", "body", 1000, env));
}
TEST(IoResynth, CorruptStateIsRejectedCleanly) {
  const auto directory = std::string(std::getenv("TEST_TMPDIR")) + "/io_corrupt";
  std::filesystem::create_directories(directory);
  for (const auto* text : {"{", "{\"schema\":1}", "{\"schema\":1,\"period_ps\":1000,\"regions\":{\"c\":{}}}"}) {
    {
      std::ofstream out(directory + "/io_timing.json");
      out << text;
    }
    EXPECT_THROW(Io_store(directory, 1000), std::runtime_error);
  }
}
}  // namespace livehd::synth
