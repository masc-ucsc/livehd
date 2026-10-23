// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "magnet_report.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "rapidjson/document.h"
namespace livehd::placement {
TEST(Placement, MagnetReportContainsGlobalMembershipDensityFramesAndSafePlaybackData) {
  namespace dp = DreamPlace::native;
  dp::Design d;
  d.layout      = {0, 0, 200, 200, 1, 1};
  d.num_movable = 2;
  d.node_x      = {20, 34};
  d.node_y      = {20, 20};
  d.node_size_x = d.node_size_y = {10, 10};
  d.node_halo_x = d.node_halo_y = {2, 2};
  d.node_macro                  = {1, 1};
  d.node_align_class = d.node_snap_class = {1, 77};
  d.build_maps();
  dp::Options o;
  o.magnet.enabled = true;
  o.max_iterations = 200;
  o.bins_x = o.bins_y   = 8;
  o.target_density      = .9;
  o.density_upper_bound = true;
  o.add_fillers = o.detailed_place = false;
  o.macro_only_legalize            = true;
  const auto placed                = dp::place(d, o);
  ASSERT_NE(placed.magnet, nullptr);
  ASSERT_EQ(placed.magnet->mapping.groups.size(), 1);
  const auto directory = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "macro_playback";
  std::filesystem::create_directories(directory);
  // The default fast JSON parser rounds this decision by one ulp. Relaying
  // recorded control measurements must preserve the original double.
  std::ofstream(directory / "macro_warp_stability.jsonl")
      << R"({"iteration":500,"previous_weight":0.85,"weight":0.8,"decision":"decrease","macro_stability_fraction":0.14753160985486385,"reference_peak_dbu":776927.5580486156,"window":10,"target":0.1})"
      << '\n';
  write_magnet_report(*placed.magnet, {"color_a/macro</script>", "color_b/macro"}, 1000, directory.string());
  std::ifstream       json_file(directory / "magnet.json");
  const std::string   text{std::istreambuf_iterator<char>(json_file), {}};
  rapidjson::Document report;
  report.Parse(text.c_str());
  ASSERT_FALSE(report.HasParseError());
  // Parse the result at full precision too, independently of the producer.
  report.Parse<rapidjson::kParseFullPrecisionFlag>(text.c_str());
  ASSERT_FALSE(report.HasParseError());
  ASSERT_EQ(report["warp_stability"].Size(), 1);
  EXPECT_EQ(report["warp_stability"][0]["macro_stability_fraction"].GetDouble(), 0.14753160985486385);
  EXPECT_DOUBLE_EQ(report["start_displacement"].GetDouble(), .1);
  EXPECT_FALSE(report.HasMember("start_overflow"));
  ASSERT_FALSE(report["start_iteration"].IsNull());
  EXPECT_LE(report["start_observed_displacement"].GetDouble(), .1);
  ASSERT_FALSE(report["motion_history"].Empty());
  EXPECT_FALSE(report["motion_history"][0]["window_complete"].GetBool());
  EXPECT_FALSE(report["motion_history"][0]["center_step_valid"].GetBool());
  EXPECT_EQ(report["motion_history"][0]["physical_macro_count"].GetInt(), 2);
  ASSERT_EQ(report["groups"].Size(), 1);
  EXPECT_EQ(report["groups"][0]["members"].Size(), 2);
  ASSERT_GT(report["frames"].Size(), 2);
  ASSERT_FALSE(report["rounds"].Empty());
  ASSERT_FALSE(report["rounds"][0]["candidates"].Empty());
  EXPECT_EQ(report["rounds"][0]["candidates"][0]["gap_region"].Size(), 4);
  for (const auto& frame : report["frames"].GetArray()) {
    EXPECT_EQ(frame["macros"].Size(), 2);
    EXPECT_EQ(frame["density"].Size(), frame["bins_x"].GetInt() * frame["bins_y"].GetInt());
  }
  std::ifstream     html_file(directory / "macro_animation.html");
  const std::string html{std::istreambuf_iterator<char>(html_file), {}};
  EXPECT_NE(html.find("\\u003c/script>"), std::string::npos);
  EXPECT_EQ(html.find("macro</script>"), std::string::npos);
  EXPECT_NE(html.find("Previous"), std::string::npos);
  EXPECT_NE(html.find("candidate decisions"), std::string::npos);
}
}  // namespace livehd::placement
