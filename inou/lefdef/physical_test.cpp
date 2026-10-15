#include "physical.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
namespace livehd::physical {
TEST(Physical, Sky130GeometryAndRc) {
  const auto tech = read_lef({"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"});
  EXPECT_GT(tech.dbu, 0);
  EXPECT_NEAR(tech.macros.at("sky130_fd_sc_hd__inv_1").width, 1.38, 1e-9);
  EXPECT_FALSE(tech.macros.at("sky130_fd_sc_hd__inv_1").pins.at("Y").shapes.empty());
  EXPECT_GT(tech.layers.at("met1").resistance, 0);
}
TEST(Physical, DefIdentityRoundtrip) {
  Floorplan fp;
  fp.design = "top";
  fp.die    = {0, 0, 10000, 10000};
  fp.rows.push_back({"row0", "unithd", 0, 0, 10, 460, Orientation::FS});
  Ann_place place(460, 0, 1380, 2720);
  place.orientation = Orientation::FS;
  place.status      = Status::FIXED;
  fp.components.push_back({"block.gate[0]", "sky130_fd_sc_hd__inv_1", place});
  fp.terminals.push_back({"a[0]", "wire0", "INPUT", 0, 1360, Orientation::N, Status::FIXED, {{"met1", {0, -10, 10, 10}}}});
  fp.nets.push_back({
      "wire0",
      {{"PIN", "a[0]"}, {"block.gate[0]", "A"}}
  });
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "roundtrip.def";
  write_def(fp, path.string());
  EXPECT_NO_THROW(verify_def_roundtrip(fp, read_def(path.string())));
  auto broken = read_def(path.string());
  broken.terminals[0].x++;
  EXPECT_THROW(verify_def_roundtrip(fp, broken), std::runtime_error);
}
TEST(Physical, OrientationTransforms) {
  EXPECT_EQ(orient_point(1, 2, 10, 20, Orientation::FS), (std::pair<double, double>{1, 18}));
  EXPECT_EQ(orient_point(1, 2, 10, 20, Orientation::FN), (std::pair<double, double>{9, 2}));
  EXPECT_EQ(orient_point(1, 2, 10, 20, Orientation::W), (std::pair<double, double>{18, 1}));
}
}  // namespace livehd::physical
