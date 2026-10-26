// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_cache.hpp"

#include <fstream>

#include "gtest/gtest.h"

namespace lhd {
namespace {
namespace fs = std::filesystem;
class PhysicalCache : public ::testing::Test {
protected:
  fs::path root;
  void     SetUp() override {
    root = fs::path(std::getenv("TEST_TMPDIR")) / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    fs::create_directories(root);
    write("compiled/library.txt", "original source and interfaces");
    write("compiled/graph/body.bin", "compiled attributes and logic");
    write("technology.lib", "original timing model");
    write("net/library.txt", "repaired color instances and inter-color buffers");
    write("net/graph/body.bin", "repaired masters, connectivity, positions and RC");
    write("place/out.def", "final DEF");
    write("place/qor.json", "{}");
    write("place/preroute.json", "{\"accepted_nets\":1,\"vias\":2}");
    write("place/power_grid.json", "{\"profile\":\"pg_asap7\",\"channels\":[]}");
    write("place/route_options.json", "{\"global_model\":\"auto\"}");
    write("place/sat_route_grid.json", "{\"bins_x\":128,\"horizontal\":[0.5]}");
    write("place/sat_route_guard.json", "{\"accepted\":true}");
    write("place/io_resynth.json", "{\"boundary_ps\":213.3134765625}");
    write("place/magnet.json", "{\"groups\":[{\"id\":1,\"members\":[\"red/ram\",\"blue/ram\"]}]}");
    write("place/macro_animation.html", "<!doctype html><title>Measured macro frames</title>");
    write("place/macro/fixed.def", "ordinary fixed member coordinates");
    write("place/macro/gp_history.json", "{\"magnet_round\":1}");
    write("place/macro/gp_history.jsonl", "{\"macro_move_max\":0.01}\n");
  }
  void write(const fs::path& path, const std::string& text) {
    fs::create_directories((root / path).parent_path());
    std::ofstream(root / path) << text;
  }
  Physical_cache cache(std::string descriptor = "engine and resolved options") {
    return Physical_cache(root / "cache", root / "compiled", std::move(descriptor), {(root / "technology.lib").string()});
  }
};

TEST_F(PhysicalCache, RestoresTheCompleteRepairedGenerationAcrossFreshInstances) {
  cache().publish(root / "net", root / "place");
  for (int run = 0; run < 2; ++run) {
    auto next = cache();
    ASSERT_TRUE(next.restore(root / "restored/net", root / "restored/place")) << next.reason();
    for (const auto& entry : fs::recursive_directory_iterator(root / "net")) {
      if (entry.is_regular_file()) {
        const auto    relative = entry.path().lexically_relative(root / "net");
        std::ifstream a(entry.path()), b(root / "restored/net" / relative);
        EXPECT_EQ((std::string{std::istreambuf_iterator<char>(a), {}}), (std::string{std::istreambuf_iterator<char>(b), {}}));
      }
    }
    std::ifstream route(root / "restored/place/route_options.json");
    EXPECT_EQ((std::string{std::istreambuf_iterator<char>(route), {}}), "{\"global_model\":\"auto\"}");
    std::ifstream grid(root / "restored/place/sat_route_grid.json");
    EXPECT_EQ((std::string{std::istreambuf_iterator<char>(grid), {}}), "{\"bins_x\":128,\"horizontal\":[0.5]}");
    std::ifstream guard(root / "restored/place/sat_route_guard.json");
    EXPECT_EQ((std::string{std::istreambuf_iterator<char>(guard), {}}), "{\"accepted\":true}");
    std::ifstream timing(root / "restored/place/io_resynth.json");
    EXPECT_EQ((std::string{std::istreambuf_iterator<char>(timing), {}}), "{\"boundary_ps\":213.3134765625}");
    for (const auto* name : {"preroute.json", "power_grid.json", "magnet.json", "macro_animation.html", "macro/fixed.def",
                             "macro/gp_history.json", "macro/gp_history.jsonl"}) {
      std::ifstream original(root / "place" / name), restored(root / "restored/place" / name);
      ASSERT_TRUE(restored) << name;
      EXPECT_EQ((std::string{std::istreambuf_iterator<char>(original), {}}),
                (std::string{std::istreambuf_iterator<char>(restored), {}}));
    }
  }
}

TEST_F(PhysicalCache, LateDependenciesKeyTheGeneration) {
  // The domino LEF exists only after the mapper ran: it is added after
  // construction and must still key the generation.
  write("domino_cells.lef", "two-row abstracts");
  auto first = cache();
  first.add_dependency((root / "domino_cells.lef").string());
  first.publish(root / "net", root / "place");
  auto same = cache();
  same.add_dependency((root / "domino_cells.lef").string());
  EXPECT_TRUE(same.restore(root / "restored/net", root / "restored/place")) << same.reason();
  EXPECT_FALSE(cache().restore(root / "restored/net", root / "restored/place"));
  write("domino_cells.lef", "regenerated abstracts");
  auto changed = cache();
  changed.add_dependency((root / "domino_cells.lef").string());
  EXPECT_FALSE(changed.restore(root / "restored/net", root / "restored/place"));
  EXPECT_EQ(changed.reason(), "technology or timing inputs changed");
}

TEST_F(PhysicalCache, RejectsEditedDesignAttributesOptionsAndTimingBytes) {
  cache().publish(root / "net", root / "place");
  EXPECT_FALSE(cache("different physical options").restore(root / "restored/net", root / "restored/place"));
  const auto original_time = fs::last_write_time(root / "technology.lib");
  write("technology.lib", "modified timing model");
  fs::last_write_time(root / "technology.lib", original_time);
  EXPECT_FALSE(cache().restore(root / "restored/net", root / "restored/place"));
  write("technology.lib", "original timing model");
  write("compiled/graph/body.bin", "changed physical/synthesis policy attribute");
  EXPECT_FALSE(cache().restore(root / "restored/net", root / "restored/place"));
  EXPECT_FALSE(fs::exists(root / "restored/net"));
}

TEST_F(PhysicalCache, RejectsCorruptedPrerouteReportBeforeRestoringGeometry) {
  cache().publish(root / "net", root / "place");
  write("cache/artifacts/place/preroute.json", "{\"accepted_nets\":0}");
  EXPECT_FALSE(cache().restore(root / "restored/net", root / "restored/place"));
  EXPECT_FALSE(fs::exists(root / "restored/net"));
  EXPECT_FALSE(fs::exists(root / "restored/place/out.def"));
}

TEST_F(PhysicalCache, RestoringWithoutPrerouteDropsStaleManagedReports) {
  fs::remove(root / "place/preroute.json");
  fs::remove(root / "place/power_grid.json");
  cache().publish(root / "net", root / "place");
  write("restored/place/preroute.json", "stale accepted routes");
  write("restored/place/power_grid.json", "stale grid channels");
  write("restored/place/notes.txt", "unmanaged caller output");
  ASSERT_TRUE(cache().restore(root / "restored/net", root / "restored/place"));
  EXPECT_FALSE(fs::exists(root / "restored/place/preroute.json"));
  EXPECT_FALSE(fs::exists(root / "restored/place/power_grid.json"));
  EXPECT_TRUE(fs::exists(root / "restored/place/notes.txt"));
  EXPECT_TRUE(fs::exists(root / "restored/place/out.def"));
}

TEST_F(PhysicalCache, RejectsPartialCorruptAndUnexpectedArtifactsBeforeRestore) {
  cache().publish(root / "net", root / "place");
  write("cache/artifacts/net/graph/body.bin", "truncated");
  EXPECT_FALSE(cache().restore(root / "restored/net", root / "restored/place"));
  cache().publish(root / "net", root / "place");
  write("cache/artifacts/net/extra.bin", "unrecorded state");
  EXPECT_FALSE(cache().restore(root / "restored/net", root / "restored/place"));
  cache().publish(root / "net", root / "place");
  fs::remove(root / "cache/artifacts/place/out.def");
  EXPECT_FALSE(cache().restore(root / "restored/net", root / "restored/place"));
  write("cache/manifest.json", "{");
  EXPECT_FALSE(cache().restore(root / "restored/net", root / "restored/place"));
  EXPECT_FALSE(fs::exists(root / "restored/net"));
}
}  // namespace
}  // namespace lhd
