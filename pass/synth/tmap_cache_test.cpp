// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tmap_cache.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

#include "hash_util.hpp"

#include "gtest/gtest.h"

namespace livehd::synth {
namespace {
namespace fs = std::filesystem;
class TmapCache : public ::testing::Test {
protected:
  fs::path directory;
  void     SetUp() override {
    auto path = (fs::temp_directory_path() / "tmap-cache-XXXXXX").string();
    ASSERT_NE(mkdtemp(path.data()), nullptr);
    directory = path;
  }
  void TearDown() override { fs::remove_all(directory); }
};

TEST_F(TmapCache, DisabledAndCancelledCachesDoNotCreateStorage) {
  Tmap_cache disabled("", 1, 1024);
  EXPECT_EQ(disabled.regions(), nullptr);
  EXPECT_FALSE(disabled.save());
  unsigned   calls = 0;
  const auto path  = directory / "cancelled";
  Tmap_cache refused(path.string(), 1, 1024, [&](std::string_view) {
    ++calls;
    return false;
  });
  EXPECT_EQ(refused.regions(), nullptr);
  EXPECT_TRUE(refused.refused());
  EXPECT_FALSE(refused.save());
  EXPECT_EQ(calls, 1U);
  EXPECT_FALSE(fs::exists(path));
}

TEST_F(TmapCache, MalformedPointerIsAColdMissWithoutChangingPublishedData) {
  for (const auto& name : {std::string("../outside"), std::string(65, 'x'), std::string("entry-../..")}) {
    std::ofstream(directory / "current") << name;
    {
      Tmap_cache cache(directory.string(), 1, 1024);
      ASSERT_NE(cache.regions(), nullptr);
      EXPECT_TRUE(cache.invalid());
      EXPECT_FALSE(cache.refused());
      EXPECT_TRUE(cache.save());  // no mapped rows to publish
    }
    std::ifstream in(directory / "current");
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), name);
    EXPECT_EQ(std::distance(fs::directory_iterator(directory), fs::directory_iterator{}), 1);
  }
}

// The published integrity record of a generation: the same file walk and
// FNV-1a fold as Tmap_cache::inventory, computed independently so each fixture
// below is rejected by exactly one admission check, not by a bad checksum.
std::string integrity_of(const fs::path& generation) {
  std::vector<fs::path> files;
  uint64_t              total = 0;
  for (const auto name : {"mapped", "mapped_pre"}) {
    for (const auto& entry : fs::recursive_directory_iterator(generation / name)) {
      if (entry.is_regular_file()) {
        files.push_back(entry.path().lexically_relative(generation));
        total += entry.file_size();
      }
    }
  }
  std::sort(files.begin(), files.end());
  uint64_t hash = hash_util::fnv1a64("tmap-cache-v1");
  for (const auto& relative : files) {
    hash = hash_util::fnv1a64(relative.generic_string(), hash);
    std::ifstream     in(generation / relative, std::ios::binary);
    const std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    hash = hash_util::fnv1a64_u64(body.size(), hash);
    hash = hash_util::fnv1a64(body, hash);
  }
  return "tmap-cache-v1 " + std::to_string(files.size()) + " " + std::to_string(total) + " " + std::to_string(hash) + "\n";
}

TEST_F(TmapCache, OversizedAndLinkedSnapshotsAreRejectedBeforeGraphLoading) {
  const auto generation = directory / "entry-AAAAAA";
  fs::create_directories(generation / "mapped");
  fs::create_directories(generation / "mapped_pre");
  std::ofstream(directory / "current") << "entry-AAAAAA";
  std::ofstream(generation / "mapped" / "abc_cache.json") << "{}";  // a cold, well-formed manifest
  const auto body = generation / "mapped" / "body";
  std::ofstream(body) << std::string(1025, 'x');
  std::ofstream(generation / "integrity") << integrity_of(generation);
  constexpr uint64_t big = 1 << 20;
  {
    // Control: a matching integrity record within the byte limit is reused.
    Tmap_cache cache(directory.string(), 1, big);
    EXPECT_FALSE(cache.invalid());
    EXPECT_FALSE(cache.refused());
    EXPECT_NE(cache.regions(), nullptr);
  }
  {
    // Only the byte admission can reject the same, otherwise valid generation.
    Tmap_cache cache(directory.string(), 1, 1024);
    EXPECT_TRUE(cache.invalid());
    EXPECT_FALSE(cache.refused());
    EXPECT_NE(cache.regions(), nullptr);
  }
  // A symlink to byte-identical content keeps the integrity record valid, so
  // only the link check can reject it.
  const auto external = directory / "external";
  fs::copy_file(body, external);
  fs::remove(body);
  fs::create_symlink(external, body);
  ASSERT_EQ(integrity_of(generation), [&] {
    std::ifstream in(generation / "integrity");
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }());
  Tmap_cache cache(directory.string(), 1, big);
  EXPECT_TRUE(cache.invalid());
  EXPECT_NE(cache.regions(), nullptr);
}
}  // namespace
}  // namespace livehd::synth
