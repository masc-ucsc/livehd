// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "evidence.hpp"

#include <stdexcept>

#include "gtest/gtest.h"
#include "rapidjson/document.h"

namespace livehd::usyn {
TEST(Evidence, PackedDecisionValidatesAgainstItsRegion) {
  const auto evidence = pack_evidence(R"({"region":"top__c1","status":"abc_opt","domino":3})");
  ASSERT_FALSE(evidence.empty());
  EXPECT_TRUE(valid_evidence(evidence));
  EXPECT_TRUE(valid_evidence(evidence, "top__c1"));
  EXPECT_FALSE(valid_evidence(evidence, "top__c2"));
  EXPECT_FALSE(valid_evidence("{\"schema_version\":1,\"decision\":{\"region\":\"top__c1\"},\"witnesses\":[]}"));
  EXPECT_FALSE(valid_evidence("not json"));
}

TEST(Evidence, RejectsADecisionWithoutARegion) { EXPECT_THROW(pack_evidence(R"({"status":"abc_opt"})"), std::runtime_error); }

TEST(Evidence, ReplayLabelsTheCachedDecisionHistorical) {
  const auto          evidence = pack_evidence(R"({"region":"old__c1","status":"abc_tmap"})");
  const auto          row      = replay_evidence("new__c1", "old__c1", evidence);
  rapidjson::Document doc;
  doc.Parse(row.c_str());
  ASSERT_FALSE(doc.HasParseError());
  EXPECT_STREQ(doc["region"].GetString(), "new__c1");
  EXPECT_STREQ(doc["cached_region"].GetString(), "old__c1");
  EXPECT_STREQ(doc["metrics_scope"].GetString(), "historical_search");
  EXPECT_STREQ(doc["decision"]["status"].GetString(), "abc_tmap");
  EXPECT_THROW(replay_evidence("new__c1", "other__c1", evidence), std::runtime_error);
}
}  // namespace livehd::usyn
