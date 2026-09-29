// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "provenance.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "gtest/gtest.h"
#include "rapidjson/document.h"

namespace {
namespace fs = std::filesystem;
struct Provenance : testing::Test {
  fs::path root;
  void     SetUp() override {
    auto pattern = (fs::temp_directory_path() / "synth-provenance-XXXXXX").string();
    ASSERT_NE(mkdtemp(pattern.data()), nullptr);
    root = pattern;
  }
  void TearDown() override { fs::remove_all(root); }
  void put(const fs::path& path, std::string_view value) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << value;
  }
  std::string context(const fs::path& path) { return "{\"inputs\":[\"" + path.string() + "\"],\"argv\":[\"lhd\",\"synth\"]}"; }
  rapidjson::Document manifest(std::string_view name) {
    std::ifstream       input(root / name / "manifest.json");
    const std::string   text{std::istreambuf_iterator<char>(input), {}};
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    EXPECT_FALSE(doc.HasParseError());
    return doc;
  }
};
TEST_F(Provenance, CapturesStableInputBytesWithSha256AndIndependentSnapshots) {
  put(root / "input/design.v", "abc");
  put(root / "input/nested/empty", "");
  const auto          summary = livehd::usyn::archive_provenance(root / "archive", context(root / "input"), "mapper-v1");
  rapidjson::Document info;
  info.Parse(summary.c_str());
  ASSERT_FALSE(info.HasParseError());
  EXPECT_TRUE(info["capture_complete"].GetBool());
  EXPECT_EQ(info["captured_files"].GetUint(), 2);
  auto doc = manifest("archive");
  ASSERT_EQ(doc["files"].Size(), 2);
  EXPECT_EQ(std::string(doc["files"][0]["sha256"].GetString()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(std::string(doc["files"][1]["sha256"].GetString()), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  put(root / "input/design.v", "semantic edit");
  std::ifstream copy(root / "archive" / doc["files"][0]["blob"].GetString());
  std::string   value;
  copy >> value;
  EXPECT_EQ(value, "abc");
  EXPECT_FALSE(doc["dependency_closure_certified"].GetBool());
  EXPECT_THROW(livehd::usyn::archive_provenance(root / "archive", context(root / "input"), "mapper-v1"), std::runtime_error);
}
TEST_F(Provenance, LimitsAndUnavailableInputsRemainExplicitlyIncomplete) {
  put(root / "large", "abcd");
  livehd::usyn::archive_provenance(root / "limited", context(root / "large"), "v1", {3, 5});
  auto limited = manifest("limited");
  EXPECT_FALSE(limited["capture_complete"].GetBool());
  EXPECT_EQ(std::string(limited["files"][0]["status"].GetString()), "byte_limit");
  livehd::usyn::archive_provenance(root / "missing", context(root / "absent"), "v1");
  EXPECT_FALSE(manifest("missing")["capture_complete"].GetBool());
  livehd::usyn::archive_provenance(root / "unknown", "", "v1");
  EXPECT_FALSE(manifest("unknown")["capture_complete"].GetBool());
  put(root / "tree/a", "a");
  put(root / "tree/b", "b");
  livehd::usyn::archive_provenance(root / "entries", context(root / "tree"), "v1", {100, 1});
  EXPECT_FALSE(manifest("entries")["capture_complete"].GetBool());
}
TEST_F(Provenance, RefusesRecursiveArchivesAndDirectorySymlinks) {
  put(root / "input/a", "a");
  fs::create_directory_symlink(root / "input", root / "input/loop");
  livehd::usyn::archive_provenance(root / "links", context(root / "input"), "v1");
  EXPECT_FALSE(manifest("links")["capture_complete"].GetBool());
  livehd::usyn::archive_provenance(root / "nested", context(root), "v1");
  auto doc = manifest("nested");
  EXPECT_FALSE(doc["capture_complete"].GetBool());
  EXPECT_EQ(std::string(doc["files"][0]["status"].GetString()), "archive_overlaps_input");
}
}  // namespace
