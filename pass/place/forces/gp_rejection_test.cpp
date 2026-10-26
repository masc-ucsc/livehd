// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gp_rejection.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

#include "gtest/gtest.h"
#include "rapidjson/document.h"

namespace tc   = livehd::placement::timeclosure;
namespace late = livehd::placement::late;
namespace ph   = livehd::physical;
namespace {
class GpRejection : public ::testing::Test {
protected:
  std::filesystem::path root;
  void                  SetUp() override {
    root = std::filesystem::path(::testing::TempDir()) / "gp_rejection"
           / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::create_directories(root);
  }
  void               TearDown() override { std::filesystem::remove_all(root); }
  static std::string text(const std::filesystem::path& path) {
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), {});
  }
  static rapidjson::Document parse(const std::filesystem::path& path) {
    rapidjson::Document json;
    const auto          data = text(path);
    json.Parse<rapidjson::kParseFullPrecisionFlag>(data.c_str());
    if (json.HasParseError()) {
      throw std::runtime_error("invalid diagnostic JSON");
    }
    return json;
  }
  static late::Result result() {
    late::Result result;
    result.certified                     = true;
    result.topology_generation           = 42;
    result.model_generation              = 67;
    result.characterized_boundary_policy = true;
    result.bounded_boundary_policy       = true;
    result.clocks["main"]                = {.period_ps = 100, .timed_endpoints = 1};
    result.pins.resize(2);
    result.pins[0].name     = "driver:Y";
    result.pins[1].name     = "end:A";
    result.pins[1].endpoint = true;
    for (unsigned t = 0; t < 2; ++t) {
      result.pins[0].transitions[t] = {.arrival_ps = 1. + t, .slew_ps = .1, .load_ff = .2};
      result.pins[1].transitions[t] = {
          .arrival_ps  = 10. + t,
          .required_ps = 100.,
          .slew_ps     = 2. + t,
          .load_ff     = .3,
          .predecessor = late::Predecessor{0, t}
      };
    }
    result.wns_ps               = 89;
    result.achievable_period_ps = 11;
    return result;
  }
  static late::Result restore(const rapidjson::Value& json) {
    late::Result result;
    result.certified                     = json["certified"].GetBool();
    result.characterized_boundary_policy = json["characterized_driver_boundary"].GetBool();
    result.bounded_boundary_policy       = json["bounded_driver_boundary"].GetBool();
    for (auto clock = json["clocks"].MemberBegin(); clock != json["clocks"].MemberEnd(); ++clock) {
      result.clocks[clock->name.GetString()]
          = {.period_ps = clock->value["period_ps"].GetDouble(), .timed_endpoints = clock->value["timed_endpoints"].GetUint64()};
    }
    for (const auto& p : json["pins"].GetArray()) {
      late::Pin_result pin;
      pin.name     = p["name"].GetString();
      pin.endpoint = p["endpoint"].GetBool();
      for (unsigned t = 0; t < 2; ++t) {
        const auto& s        = p["transitions"][t];
        const auto  quantity = [&](const char* field) -> std::optional<double> {
          return s[field].IsNull() ? std::nullopt : std::optional<double>(s[field].GetDouble());
        };
        pin.transitions[t] = {.arrival_ps  = quantity("arrival_ps"),
                              .required_ps = quantity("required_ps"),
                              .slew_ps     = quantity("slew_ps"),
                              .load_ff     = quantity("load_ff")};
      }
      result.pins.push_back(std::move(pin));
    }
    return result;
  }
};
TEST_F(GpRejection, SavedStatesReproduceTheRejectingComparisonAndExactRcFrame) {
  const auto reference                         = result();
  auto       continuous                        = reference;
  continuous.pins[1].transitions[0].arrival_ps = 30.123456789012345;
  continuous.pins[1].transitions[1].slew_ps    = 6;
  const auto comparison                        = late::compare_timing(continuous, reference);
  ASSERT_FALSE(comparison.comparison_gates_pass);
  late::Star_context star;
  star.stats.calibrated                = 3;
  star.stats.reused                    = 7;
  star.stats.max_displacement_fraction = .005;
  ph::Parasitics rc;
  rc.identity = "exact-rejected-frame";
  rc.nets     = {
      {"driver:Y", {{"driver:Y", .12345678901234568}, {"end:A", .9876543210987654}}, {{0, 1, 123.45678901234567}}}
  };
  const std::map<std::string, std::pair<double, double>> locations{
      {"driver:Y", {.12345678901234568, 4.}},
      {   "end:A",  {9., .9876543210987654}}
  };
  const auto prefix = (root / "failed").string();
  tc::write_gp_timing_rejection(prefix, {reference, continuous, comparison, star}, rc, locations, 110, .37);
  auto report = parse(prefix + ".comparison.json");
  EXPECT_TRUE(report["diagnostic_only"].GetBool());
  EXPECT_FALSE(report["optimization_state_committed"].GetBool());
  EXPECT_EQ(report["iteration"].GetInt(), 110);
  EXPECT_EQ(report["reference"]["topology_generation"].GetUint64(), 42);
  EXPECT_EQ(report["reference"]["model_generation"].GetUint64(), 67);
  EXPECT_EQ(report["continuous"]["pins"][1]["transitions"][0]["predecessor"][0].GetUint64(), 0);
  const auto replay = late::compare_timing(restore(report["continuous"]), restore(report["reference"]));
  EXPECT_EQ(replay.numerical_gates_pass, comparison.numerical_gates_pass);
  EXPECT_EQ(replay.ranking_gates_pass, comparison.ranking_gates_pass);
  EXPECT_EQ(replay.ranking.jaccard, comparison.ranking.jaccard);
  EXPECT_EQ(replay.ranking.tau_b, comparison.ranking.tau_b);
  for (unsigned i = 0; i < 4; ++i) {
    EXPECT_EQ(replay.fields[i].tolerance_failures, comparison.fields[i].tolerance_failures);
    EXPECT_EQ(replay.fields[i].compared, comparison.fields[i].compared);
    EXPECT_EQ(replay.fields[i].maximum, comparison.fields[i].maximum);
  }
  EXPECT_EQ(ph::deserialize_parasitics_exact(text(prefix + ".rc.json")).nets, rc.nets);
  const auto pins = parse(prefix + ".pins.json");
  for (const auto& [name, xy] : locations) {
    EXPECT_EQ(pins[name.c_str()][0].GetDouble(), xy.first);
    EXPECT_EQ(pins[name.c_str()][1].GetDouble(), xy.second);
  }
}
TEST_F(GpRejection, UnavailableRankAndUndeclaredLoadToleranceRemainNull) {
  auto reference                   = result();
  reference.pins[1].transitions[1] = reference.pins[1].transitions[0];
  const auto comparison            = late::compare_timing(reference, reference);
  ASSERT_FALSE(comparison.ranking.tau_b.has_value());
  late::Star_context star;
  const auto         prefix = (root / "ties").string();
  tc::write_gp_timing_rejection(prefix, {reference, reference, comparison, star}, {}, {}, 10, 0);
  const auto report = parse(prefix + ".comparison.json");
  EXPECT_TRUE(report["comparison"]["ranking"]["tau_b"].IsNull());
  EXPECT_TRUE(report["comparison"]["fields"]["load_ff"]["tolerance_failures"].IsNull());
  EXPECT_EQ(report["comparison"]["fields"]["arrival_ps"]["tolerance_failures"].GetUint64(), 0);
}
TEST_F(GpRejection, MissingOutputDirectoryAndInvalidMetadataAreExplicitFailures) {
  const auto         reference  = result();
  const auto         comparison = late::compare_timing(reference, reference);
  late::Star_context star;
  EXPECT_THROW(
      tc::write_gp_timing_rejection((root / "missing/frame").string(), {reference, reference, comparison, star}, {}, {}, 10, .1),
      std::runtime_error);
  EXPECT_THROW(tc::write_gp_timing_rejection((root / "invalid").string(), {reference, reference, comparison, star}, {}, {}, -1, .1),
               std::invalid_argument);
  EXPECT_FALSE(std::filesystem::exists(root / "invalid.rc.json"));
}
}  // namespace
