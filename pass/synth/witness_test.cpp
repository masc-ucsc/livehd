// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "witness.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>

#include "gtest/gtest.h"
#include "rapidjson/document.h"

namespace livehd::synth {
namespace {
class Witness : public ::testing::Test {
protected:
  std::string directory;
  void        SetUp() override {
    directory = (std::filesystem::temp_directory_path() / "livehd-witness-XXXXXX").string();
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
  }
  void TearDown() override { std::filesystem::remove_all(directory); }
};

TEST_F(Witness, CompleteRecordsAreBoundedAndOmissionsAreExplicit) {
  Logic_network source;
  source.outputs    = {source.add_source()};
  const auto result = optimize(source);
  ASSERT_EQ(result.status, Status::feasible);
  const auto&     a    = result.attempts[0];
  const auto      path = directory + "/full.jsonl";
  Witness_archive full(path, "test-version", 10000);
  const auto      first = full.append("quoted\"\nregion", 0, "initial", source, a.network, a.recipe);
  EXPECT_EQ(first.record, 0);
  EXPECT_EQ(first.status, "archived");
  const auto bytes = full.bytes();
  full.close();
  std::ifstream     input(path);
  const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  EXPECT_EQ(text.size(), bytes);
  EXPECT_NE(text.find("quoted\\\"\\nregion"), std::string::npos);
  EXPECT_NE(text.find("source_digest_fnv1a64"), std::string::npos);
  EXPECT_NE(text.find("test-version"), std::string::npos);
  EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 1);
  Witness_archive capped(directory + "/capped.jsonl", "test-version", bytes);
  EXPECT_EQ(capped.append("quoted\"\nregion", 0, "initial", source, a.network, a.recipe).status, "archived");
  EXPECT_EQ(capped.append("quoted\"\nregion", 0, "initial", source, a.network, a.recipe).status, "size_limit");
  EXPECT_EQ(capped.records(), 1);
  EXPECT_EQ(capped.omitted(), 1);
  capped.close();
  EXPECT_EQ(std::filesystem::file_size(directory + "/capped.jsonl"), bytes);
  Witness_archive disabled(directory + "/disabled.jsonl", "test-version", 0);
  EXPECT_EQ(disabled.append("r", 0, "initial", source, a.network, a.recipe).status, "disabled");
  EXPECT_EQ(disabled.omitted(), 1);
  disabled.close();
  EXPECT_EQ(std::filesystem::file_size(directory + "/disabled.jsonl"), 0);
}

TEST_F(Witness, CreationFailureDoesNotPretendToArchive) { EXPECT_THROW(Witness_archive(directory, "v", 1000), std::runtime_error); }

TEST_F(Witness, SourceOnlySearchEvidenceReplaysWithoutClaimingADecomposition) {
  Logic_network source;
  source.outputs = {source.add_source()};
  Witness_archive cold(directory + "/source", "v", 100000);
  Source_boundary boundary;
  boundary.inputs = {
      {"q", "state", 0}
  };
  boundary.outputs = {
      {"d", "state", 0}
  };
  boundary.states = {
      {"register", "zero", 0, 0}
  };
  const auto record = cold.append_source("old", source, &boundary);
  ASSERT_EQ(record.record, 0);
  const std::string decision = R"({"region":"old","source_witness":{"record":0,"status":"archived"},"attempts":[]})";
  const auto        evidence = pack_evidence(decision, cold.capture_since(0));
  ASSERT_TRUE(valid_evidence(evidence, "old"));
  Witness_archive warm(directory + "/warm_source", "v", 100000);
  warm.append_source("other", source);
  const auto          replayed = replay_evidence(warm, "new", "old", evidence);
  rapidjson::Document doc;
  doc.Parse(replayed.c_str());
  ASSERT_FALSE(doc.HasParseError());
  EXPECT_EQ(doc["decision"]["source_witness"]["record"].GetInt(), 1);
  EXPECT_NE(warm.capture_since(0).find("\"kind\":\"unate_source\""), std::string::npos);
  EXPECT_NE(warm.capture_since(0).find("\"schema_version\":2"), std::string::npos);
  EXPECT_NE(warm.capture_since(0).find("\"name\":\"register\",\"init\":\"zero\""), std::string::npos);
  Witness_archive capped(directory + "/source_capped", "v", 1);
  const auto      missing = replay_evidence(capped, "new", "old", evidence);
  EXPECT_NE(missing.find("\"record\":-1,\"status\":\"size_limit\""), std::string::npos);
  EXPECT_EQ(capped.omitted(), 1);
  EXPECT_EQ(capped.records(), 0);
  EXPECT_EQ(capped.append_source("new", source).status, "size_limit");
  auto       wrong = evidence;
  const auto kind  = wrong.find("unate_source");
  ASSERT_NE(kind, std::string::npos);
  wrong.replace(kind, std::string("unate_source").size(), "unate_witness");
  EXPECT_FALSE(valid_evidence(wrong));
}

TEST_F(Witness, CacheReplayRemapsRecordsAndPreservesHistoricalDecisions) {
  Logic_network source;
  source.outputs    = {source.add_source()};
  const auto result = optimize(source);
  ASSERT_EQ(result.status, Status::feasible);
  const auto&     a = result.attempts[0];
  Witness_archive cold(directory + "/cold", "v", 100000);
  ASSERT_EQ(cold.append("unrelated", 0, "initial", source, a.network, a.recipe).record, 0);
  const auto begin = cold.bytes();
  ASSERT_EQ(cold.append("original", 2, "initial", source, a.network, a.recipe).record, 1);
  const std::string decision
      = R"({"region":"original","selected_recipe":2,"selected_candidate":0,"unate_ms":42,"region_elapsed_ms":24.646250000000002,
    "attempts":[{"recipe_index":2,"variant":"initial","witness":{"record":1,"status":"archived"}}]})";
  const auto evidence = pack_evidence(decision, cold.capture_since(begin));
  ASSERT_TRUE(valid_evidence(evidence));
  EXPECT_TRUE(valid_evidence(evidence, "original"));
  EXPECT_FALSE(valid_evidence(evidence, "wrong-row"));
  cold.close();
  Witness_archive warm(directory + "/warm", "v", 100000);
  // Reordered and renamed regions (including a fresh region between hits).
  for (const auto name : {"renamed1", "renamed2"}) {
    const auto          next   = warm.records();
    const auto          report = replay_evidence(warm, name, "original", evidence);
    rapidjson::Document doc;
    doc.Parse<rapidjson::kParseFullPrecisionFlag>(report.c_str());
    ASSERT_FALSE(doc.HasParseError());
    EXPECT_STREQ(doc["region"].GetString(), name);
    EXPECT_STREQ(doc["cached_region"].GetString(), "original");
    EXPECT_STREQ(doc["metrics_scope"].GetString(), "historical_search");
    EXPECT_EQ(doc["decision"]["unate_ms"].GetInt(), 42);
    EXPECT_EQ(doc["decision"]["region_elapsed_ms"].GetDouble(), 24.646250000000002);
    EXPECT_EQ(doc["decision"]["selected_recipe"].GetInt(), 2);
    EXPECT_EQ(doc["decision"]["attempts"][0]["witness"]["record"].GetUint64(), next);
    warm.append("fresh", 0, "initial", source, a.network, a.recipe);
  }
  EXPECT_EQ(warm.records(), 4);
  EXPECT_NE(warm.capture_since(0).find("\"reused_from\":{\"region\":\"original\",\"record\":1}"), std::string::npos);
  warm.close();
  Witness_archive capped(directory + "/capped", "v", 1);
  const auto      report = replay_evidence(capped, "renamed", "original", evidence);
  EXPECT_NE(report.find("\"record\":-1,\"status\":\"size_limit\""), std::string::npos);
  EXPECT_EQ(capped.records(), 0);
  EXPECT_EQ(capped.omitted(), 1);
  capped.close();
  EXPECT_FALSE(valid_evidence("{}"));
  EXPECT_FALSE(valid_evidence(evidence.substr(0, evidence.size() - 1)));
  EXPECT_THROW(pack_evidence(decision, ""), std::runtime_error);
  auto       wrong = evidence;
  const auto id    = wrong.find("\"record\":1");
  ASSERT_NE(id, std::string::npos);
  wrong.replace(id, 10, "\"record\":9");
  EXPECT_FALSE(valid_evidence(wrong));
}

TEST_F(Witness, HierarchicalDefinitionsUseSchemaFiveAndReplayWithoutLosingTheirEdges) {
  Logic_network source;
  for (unsigned i = 0; i < 16; ++i) {
    source.add_source();
  }
  Truth_table conjunction(2);
  conjunction.set(3, true);
  Id root = 0;
  for (Id i = 1; i < 16; ++i) {
    root = source.add_function({root, i}, conjunction);
  }
  source.outputs = {root};
  Search_options options;
  options.recipes = {
      {4, 2, 16, 2}
  };
  options.cuts_per_node = 1;
  options.cover_limit = options.divisor_limit = options.joint_limit = options.recovery_rounds = options.encoding_limit = 0;
  const auto result = optimize(source, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt = result.attempts[0];
  ASSERT_EQ(attempt.network.encodings.size(), 14);
  Witness_archive cold(directory + "/hierarchy", "v", 100000);
  ASSERT_EQ(cold.append("original", 0, "initial", source, attempt.network, attempt.recipe).record, 0);
  const auto          text = cold.capture_since(0);
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  ASSERT_FALSE(doc.HasParseError());
  EXPECT_EQ(doc["schema_version"].GetInt(), 5);
  EXPECT_GE(doc["network"]["encodings"][13]["inputs"][0].GetUint(), source.nodes.size());
  const std::string decision
      = R"({"region":"original","attempts":[{"recipe_index":0,"variant":"initial","witness":{"record":0,"status":"archived"}}]})";
  const auto evidence = pack_evidence(decision, text);
  ASSERT_TRUE(valid_evidence(evidence));
  Witness_archive warm(directory + "/hierarchy_warm", "v", 100000);
  EXPECT_FALSE(replay_evidence(warm, "renamed", "original", evidence).empty());
  EXPECT_EQ(warm.records(), 1);
  const auto          replay = warm.capture_since(0);
  rapidjson::Document copied;
  copied.Parse(replay.c_str());
  ASSERT_FALSE(copied.HasParseError());
  EXPECT_EQ(copied["schema_version"].GetInt(), 5);
  EXPECT_TRUE(copied["network"] == doc["network"]);
}
}  // namespace
}  // namespace livehd::synth
