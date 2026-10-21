// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "export_file.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "gtest/gtest.h"

namespace lhd {
namespace {
namespace fs = std::filesystem;
constexpr auto readonly = fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read;

class ExportFile : public ::testing::Test {
protected:
  fs::path root;
  void SetUp() override {
    const auto* directory = std::getenv("TEST_TMPDIR");
    ASSERT_NE(directory, nullptr);
    root = fs::path(directory) / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    fs::create_directories(root);
  }
  void TearDown() override { fs::remove_all(root); }
  void write(const fs::path& path, const std::string& bytes) { std::ofstream(path, std::ios::binary) << bytes; }
  std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
  }
};

TEST_F(ExportFile, RepeatedReadonlyCompanionExportLeavesSourceUntouched) {
  const auto source = root / "technology.lef";
  const auto target = root / "exported.lef";
  const std::string bytes = "VERSION 5.8 ;\nEND LIBRARY\n";
  write(source, bytes);
  fs::permissions(source, readonly);
  const auto source_time = fs::last_write_time(source);
  for (int run = 0; run < 3; ++run) {
    ASSERT_NO_THROW(copy_export_file(source, target));
    EXPECT_EQ(read(target), bytes);
    EXPECT_EQ(fs::status(target).permissions(), readonly);
    EXPECT_EQ(read(source), bytes);
    EXPECT_EQ(fs::status(source).permissions(), readonly);
    EXPECT_EQ(fs::last_write_time(source), source_time);
  }
}

TEST_F(ExportFile, ChangedContentReplacesReadonlyDestinationWithoutWritingItsAliases) {
  const auto source = root / "new.lef";
  const auto target = root / "exported.lef";
  const auto alias = root / "retained.lef";
  const std::string old = "old view\n";
  std::string bytes(140000, 'x');
  bytes[65536] = '\0';
  bytes.back() = '\n';
  write(source, bytes);
  fs::permissions(source, fs::perms::owner_read | fs::perms::owner_write);
  const auto source_time = fs::last_write_time(source);
  write(target, old);
  fs::permissions(target, readonly);
  fs::create_hard_link(target, alias);
  ASSERT_NO_THROW(copy_export_file(source, target));
  EXPECT_EQ(read(target), bytes);
  EXPECT_EQ(fs::status(target).permissions(), readonly);
  EXPECT_EQ(read(alias), old);
  EXPECT_EQ(fs::status(alias).permissions(), readonly);
  EXPECT_FALSE(fs::equivalent(target, alias));
  EXPECT_EQ(read(source), bytes);
  EXPECT_EQ(fs::status(source).permissions(), fs::perms::owner_read | fs::perms::owner_write);
  EXPECT_EQ(fs::last_write_time(source), source_time);
}

TEST_F(ExportFile, FailedRenameRetainsDestinationAndRemovesTemporary) {
  const auto source = root / "source.lef";
  const auto target = root / "destination";
  write(source, "new view\n");
  fs::permissions(source, readonly);
  fs::create_directory(target);
  write(target / "retained", "original directory\n");
  EXPECT_THROW(copy_export_file(source, target), fs::filesystem_error);
  EXPECT_EQ(read(target / "retained"), "original directory\n");
  EXPECT_EQ(read(source), "new view\n");
  EXPECT_EQ(fs::status(source).permissions(), readonly);
  EXPECT_EQ(std::distance(fs::directory_iterator(root), fs::directory_iterator()), 2);
}

TEST_F(ExportFile, ExportToTheSourceOrItsHardlinkIsANoop) {
  const auto source = root / "source.lef";
  const auto alias = root / "alias.lef";
  write(source, "same physical file\n");
  fs::permissions(source, readonly);
  fs::create_hard_link(source, alias);
  ASSERT_NO_THROW(copy_export_file(source, source));
  ASSERT_NO_THROW(copy_export_file(source, alias));
  EXPECT_TRUE(fs::equivalent(source, alias));
  EXPECT_EQ(read(source), "same physical file\n");
  EXPECT_EQ(fs::status(source).permissions(), readonly);
}
}  // namespace
}  // namespace lhd
