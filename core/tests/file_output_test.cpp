//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// File_output is WRITE-IF-DIFFERENT: when the file already holds exactly the
// bytes queued, the destructor leaves it — and its mtime — alone. That mtime is
// what the generated build.ninja keys on, so this class decides whether an
// unchanged design rebuilds. Two directions to pin:
//
//   * a false "same" would leave a STALE file on disk while the run reports
//     success — the dangerous direction, since the emit manifests hash the file
//     back and would happily record the stale bytes as correct;
//   * a false "different" only costs a needless rebuild, but it costs it on
//     EVERY file of EVERY run, which is the whole feature.
//
// The comparison streams `sequence` against a 64 KiB buffer, so the cases that
// matter are the ones that straddle or align with that boundary.

#include "file_output.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "gtest/gtest.h"

namespace {

std::string tmp_path(const char* name) {
  auto p = std::filesystem::temp_directory_path() / std::format("lhd_fo_{}", name);
  std::filesystem::remove(p);
  return p.string();
}

std::string slurp(const std::string& p) {
  std::ifstream     ifs(p, std::ios::binary);
  std::stringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

// Nanosecond mtime, so "was it rewritten" does not hinge on clock granularity.
long long mtime_ns(const std::string& p) {
  struct stat st {};
  if (::stat(p.c_str(), &st) != 0) {
    return -1;
  }
#if defined(__APPLE__)
  return static_cast<long long>(st.st_mtimespec.tv_sec) * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
  return static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
#endif
}

// Emit `pieces` to `path` through File_output; -> true when the file's mtime
// was left alone (i.e. the write was skipped).
bool emit_and_check_untouched(const std::string& path, const std::vector<std::string>& pieces) {
  const long long before = mtime_ns(path);
  {
    File_output fo(path);
    for (const auto& p : pieces) {
      fo.append(p);
    }
  }
  return before >= 0 && mtime_ns(path) == before;
}

TEST(FileOutput, WritesWhenAbsent) {
  const auto path = tmp_path("absent");
  {
    File_output fo(path);
    fo.append("hello ");
    fo.append("world\n");
  }
  EXPECT_EQ(slurp(path), "hello world\n");
  std::filesystem::remove(path);
}

TEST(FileOutput, IdenticalContentLeavesFileUntouched) {
  const auto path = tmp_path("same");
  {
    File_output fo(path);
    fo.append("stable bytes\n");
  }
  // Re-emitting the SAME bytes, even split across different append() calls,
  // must not touch the file: the comparison is over the concatenation.
  EXPECT_TRUE(emit_and_check_untouched(path, {"stable ", "bytes\n"}));
  EXPECT_EQ(slurp(path), "stable bytes\n");
  std::filesystem::remove(path);
}

TEST(FileOutput, DifferentContentRewrites) {
  const auto path = tmp_path("diff");
  {
    File_output fo(path);
    fo.append("aaaa");
  }
  EXPECT_FALSE(emit_and_check_untouched(path, {"aaab"}));  // same length, last byte differs
  EXPECT_EQ(slurp(path), "aaab");
  std::filesystem::remove(path);
}

TEST(FileOutput, LengthMismatchRewrites) {
  const auto path = tmp_path("len");
  {
    File_output fo(path);
    fo.append("prefix");
  }
  // A pure prefix must NOT compare equal — the size gate has to catch it, in
  // both directions (shorter and longer than what is on disk).
  EXPECT_FALSE(emit_and_check_untouched(path, {"prefix-and-more"}));
  EXPECT_EQ(slurp(path), "prefix-and-more");
  EXPECT_FALSE(emit_and_check_untouched(path, {"prefix"}));
  EXPECT_EQ(slurp(path), "prefix");
  std::filesystem::remove(path);
}

TEST(FileOutput, EmptyPiecesAndEmptyFile) {
  const auto path = tmp_path("empty");
  {
    File_output fo(path);
    fo.append("");
  }
  EXPECT_EQ(slurp(path), "");
  EXPECT_TRUE(emit_and_check_untouched(path, {"", "", ""}));
  EXPECT_FALSE(emit_and_check_untouched(path, {"", "x", ""}));
  EXPECT_EQ(slurp(path), "x");
  std::filesystem::remove(path);
}

// The read buffer is 64 KiB. A payload that straddles it, in pieces that do not
// align with it, is where a three-cursor streaming compare goes wrong.
TEST(FileOutput, StraddlesTheReadBuffer) {
  const auto path = tmp_path("big");

  std::mt19937                    rng(1234);
  std::uniform_int_distribution<> byte(32, 126);
  std::string                     payload;
  payload.reserve(200 * 1024);
  while (payload.size() < 200 * 1024) {
    payload.push_back(static_cast<char>(byte(rng)));
  }

  // Split into deliberately awkward pieces: bigger than the buffer, exactly the
  // buffer, and a 1-byte tail.
  const std::vector<std::string> pieces = {payload.substr(0, 100 * 1024),
                                           payload.substr(100 * 1024, 64 * 1024),
                                           payload.substr(164 * 1024, payload.size() - 164 * 1024 - 1),
                                           payload.substr(payload.size() - 1)};
  {
    File_output fo(path);
    for (const auto& p : pieces) {
      fo.append(p);
    }
  }
  EXPECT_EQ(slurp(path), payload);
  EXPECT_TRUE(emit_and_check_untouched(path, pieces));
  // One flipped byte PAST the first buffer-full must still be detected.
  auto mutated                = payload;
  mutated[150 * 1024]         = static_cast<char>(mutated[150 * 1024] ^ 0x1);
  EXPECT_FALSE(emit_and_check_untouched(path, {mutated}));
  EXPECT_EQ(slurp(path), mutated);
  std::filesystem::remove(path);
}

TEST(FileOutput, PrependParticipatesInTheComparison) {
  const auto path = tmp_path("prepend");
  {
    File_output fo(path);
    fo.append("body\n");
    fo.prepend("header\n");
  }
  EXPECT_EQ(slurp(path), "header\nbody\n");
  {
    // Same final bytes, reached the same way -> untouched.
    const long long before = mtime_ns(path);
    {
      File_output fo(path);
      fo.append("body\n");
      fo.prepend("header\n");
    }
    EXPECT_EQ(mtime_ns(path), before);
  }
  std::filesystem::remove(path);
}

// mark()/detach_from(): inou.cgen.sim emits a whole method, detaches it, drops
// the dead temporaries and appends the rewritten text. The bytes before the
// mark must survive untouched, and the line tallies must follow the detach (a
// source-map consumer reads append_line() after the rewrite).
TEST(FileOutput, MarkDetachRewritesTail) {
  const auto path = tmp_path("detach");
  {
    File_output fo(path);
    fo.append("keep\n");
    const auto m = fo.mark();
    fo.append("dead\n");
    fo.append("alive\n");
    EXPECT_EQ(fo.append_line(), 3U);

    auto tail = fo.detach_from(m);
    EXPECT_EQ(tail, "dead\nalive\n");
    EXPECT_EQ(fo.append_line(), 1U);

    fo.append("alive\n");  // the rewritten region
  }
  EXPECT_EQ(slurp(path), "keep\nalive\n");
  std::filesystem::remove(path);
}

TEST(FileOutput, DetachAtEndIsEmpty) {
  const auto path = tmp_path("detach_end");
  {
    File_output fo(path);
    fo.append("only\n");
    EXPECT_TRUE(fo.detach_from(fo.mark()).empty());
  }
  EXPECT_EQ(slurp(path), "only\n");
  std::filesystem::remove(path);
}

TEST(FileOutput, AbortWritesNothing) {
  const auto path = tmp_path("abort");
  {
    File_output fo(path);
    fo.append("should not appear");
    fo.abort();
  }
  EXPECT_FALSE(std::filesystem::exists(path));
}

}  // namespace
