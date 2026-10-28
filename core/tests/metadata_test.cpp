// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "metadata.hpp"

#include <array>
#include <filesystem>

#include "gtest/gtest.h"
using livehd::metadata::Bundle;
TEST(Metadata, CompactCanonicalAndUnrelatedRecords) {
  Bundle a(R"([{"class":"report","x":1},{"id":"h","class":"hint","design":"top","value":9007199254740993}])");
  Bundle b(R"([{"value":9007199254740993,"design":"top","class":"hint","id":"h"},{"class":"report","x":2}])");
  const std::array<std::string_view, 1> classes{"hint"};
  EXPECT_EQ(a.identity(classes, "top"), b.identity(classes, "top"));
  EXPECT_NE(a.json(), b.json());
  EXPECT_NE(a.json().find("9007199254740993"), std::string::npos);
  EXPECT_TRUE(a.select("hint", "other").empty());
}
TEST(Metadata, RejectAmbiguityAndMalformedInput) {
  EXPECT_THROW(Bundle("{}"), std::invalid_argument);
  EXPECT_THROW(Bundle(R"([{"class":"x","class":"y"}])"), std::invalid_argument);
  EXPECT_THROW(Bundle(R"([{"class":"x","id":"a","n":1},{"class":"x","id":"a","n":2}])"), std::invalid_argument);
  Bundle duplicate(R"([{"class":"x","id":"a"},{"id":"a","class":"x"}])");
  EXPECT_EQ(duplicate.select("x", "").size(), 1);
}
TEST(Metadata, RoundTripPreservesForeignRecords) {
  Bundle              b(R"([{"class":"foreign","payload":{"z":[1,true,null]}},{"class":"state","id":"out","x":1}])");
  rapidjson::Document r;
  r.Parse(R"({"class":"state","id":"out","x":2})");
  b.replace(r);
  auto path = std::filesystem::path(testing::TempDir()) / "metadata-roundtrip.json";
  b.write(path.string());
  auto copy = Bundle::read(path.string());
  EXPECT_EQ(copy.json(), b.json());
  EXPECT_EQ(copy.select("state", "").front()->operator[]("x").GetInt(), 2);
  EXPECT_EQ(copy.select("foreign", "").size(), 1);
  std::filesystem::remove(path);
}

TEST(Metadata, MergeIsAtomicAndRejectsCrossFileConflicts) {
  Bundle a(R"([{"class":"hint","id":"x","v":1}])");
  a.merge(Bundle(R"([{"class":"report","id":"y"},{"class":"hint","id":"x","v":1}])"));
  EXPECT_EQ(a.select("hint", "").size(), 1);
  const auto before = a.json();
  EXPECT_THROW(a.merge(Bundle(R"([{"class":"hint","id":"x","v":2}])")), std::invalid_argument);
  EXPECT_EQ(a.json(), before);
  EXPECT_EQ(a.select("report", "").size(), 1);
}
