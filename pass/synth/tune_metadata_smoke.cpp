// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gtest/gtest.h"
#include "tune_store.hpp"

TEST(TuneMetadata, ScopedReplayPreservesForeignSectionsAndValidation) {
  const std::string records
      = R"([{"class":"foreign","id":"note","value":17},{"class":"livehd.synth.tuning","id":"selection","design":"top","schema_version":1,"decisions":{"hash":{"validation":"structural","options":{"adder":"cla","multiplier":"tdm","barrel":"log","block_size":0}}}}])";
  const auto                out = testing::TempDir() + "/tuning-bundle.json";
  livehd::synth::Tune_store store("", "", out, false, true, "structural", records, "top");
  EXPECT_FALSE(store.find("hash").empty());
  livehd::synth::Tune_store other("", "", "", false, true, "structural", records, "other");
  EXPECT_TRUE(other.find("hash").empty());
  livehd::synth::Tune_store proof("", "", "", false, true, "design", records, "top");
  EXPECT_TRUE(proof.find("hash").empty());
  store.save();
  auto bundle = livehd::metadata::Bundle::read(out);
  EXPECT_EQ(bundle.select("foreign", "top").size(), 1);
  EXPECT_EQ(bundle.select("livehd.synth.tuning", "top").size(), 1);
  livehd::synth::Tune_store replay("", "", "", false, true, "structural", bundle.json(), "top");
  EXPECT_EQ(store.find("hash"), replay.find("hash"));
  std::filesystem::remove(out);
}
TEST(TuneMetadata, ConflictingDecisionsAcrossDistinctRecordsAreRejected) {
  const std::string records
      = R"([{"class":"livehd.synth.tuning","id":"a","schema_version":1,"decisions":{"hash":{"options":{"adder":"cla","multiplier":"tdm","barrel":"log","block_size":0}}}},{"class":"livehd.synth.tuning","id":"b","schema_version":1,"decisions":{"hash":{"options":{"adder":"cla","multiplier":"tdm","barrel":"log","block_size":4}}}}])";
  EXPECT_THROW((livehd::synth::Tune_store("", "", "", false, true, "structural", records)), std::runtime_error);
}
