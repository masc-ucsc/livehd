// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "placement_technology.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "place.hpp"
#include "prects_networks.hpp"
#include "repair_internal.hpp"

namespace livehd::placement {
TEST(PlacementTechnology, SharedSnapshotNeedsNoFilesDuringCheckpoints) {
  const auto root = std::filesystem::path(std::getenv("TEST_TMPDIR"));
  Options    options;
  for (const auto* name : {"sky130hd.tlef", "sky130hd.lef", "sky130hd.lib"}) {
    std::filesystem::copy_file(std::filesystem::path("inou/lefdef/tests/data") / name,
                               root / name,
                               std::filesystem::copy_options::overwrite_existing);
  }
  options.lefs           = {(root / "sky130hd.tlef").string(), (root / "sky130hd.lef").string()};
  options.liberty        = (root / "sky130hd.lib").string();
  const auto& technology = options.technology();
  const auto  roles      = prects_library(technology.cells);
  const auto  special    = detail::scan_special_pins(technology.cells, true);
  const auto  cells      = detail::inventory(options, technology.lef);
  ASSERT_FALSE(cells.empty());
  auto checkpoint = options;
  for (const auto& path : options.lefs) {
    std::filesystem::remove(path);
  }
  std::filesystem::remove(options.liberty);
  EXPECT_EQ(&checkpoint.technology(), &technology);
  EXPECT_EQ(prects_library(checkpoint.technology().cells).controls, roles.controls);
  EXPECT_EQ(detail::scan_special_pins(checkpoint.technology().cells, true), special);
  EXPECT_EQ(detail::inventory(checkpoint, checkpoint.technology().lef).size(), cells.size());
  Options independent;
  independent.lefs    = options.lefs;
  independent.liberty = options.liberty;
  EXPECT_THROW(independent.technology(), std::runtime_error);
}

TEST(PlacementTechnology, InputChangesDoNotMutateOtherSnapshots) {
  Options options;
  options.lefs          = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.liberty       = "inou/lefdef/tests/data/sky130hd.lib";
  const auto& first     = options.technology();
  auto        alternate = options;
  alternate.liberty.clear();
  const auto& second = alternate.technology();
  EXPECT_NE(&first, &second);
  EXPECT_FALSE(first.cells.combinational.empty());
  EXPECT_TRUE(second.cells.combinational.empty());
  EXPECT_NE(first.content_hash, second.content_hash);
  EXPECT_EQ(&options.technology(), &first);
}
}  // namespace livehd::placement
