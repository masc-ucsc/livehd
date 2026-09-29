// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "file_name.hpp"

#include <string>

#include "gtest/gtest.h"

namespace {

std::string repeat(char c, size_t n) { return std::string(n, c); }

}  // namespace

TEST(FileName, ShortNamesPassThroughUntouched) {
  EXPECT_EQ("", livehd::shorten_file_name(""));
  EXPECT_EQ("top.v", livehd::shorten_file_name("top.v"));
  // The cap itself is NOT long: only a name PAST it is shortened.
  const auto at_cap = repeat('x', livehd::kMaxFileNameBytes);
  EXPECT_EQ(at_cap, livehd::shorten_file_name(at_cap));
}

TEST(FileName, LongNameKeepsPrefixAndIsCappedAtTheLimit) {
  const auto name = repeat('x', livehd::kMaxFileNameBytes + 1);
  const auto out  = livehd::shorten_file_name(name);
  EXPECT_EQ(livehd::kMaxFileNameBytes, out.size());
  EXPECT_TRUE(out.starts_with(repeat('x', 135)));
  // The tail is a 64-character lowercase hex digest after a single '_'.
  EXPECT_EQ('_', out[135]);
  const auto digest = out.substr(136);
  EXPECT_EQ(64U, digest.size());
  for (const char c : digest) {
    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << c;
  }
}

TEST(FileName, DeterministicAndCollisionFreeOnASharedPrefix) {
  // The motivating shape: generated parameter specializations differing only
  // in their LAST character, well past the point a prefix truncation would
  // merge them.
  const auto base = repeat('p', 300);
  const auto a    = livehd::shorten_file_name(base + "a");
  const auto b    = livehd::shorten_file_name(base + "b");
  EXPECT_NE(a, b);
  EXPECT_EQ(a, livehd::shorten_file_name(base + "a"));  // stable across calls
}

TEST(FileName, ShortExtensionSurvivesTheShortening) {
  for (const std::string ext : {".v", ".sv", ".prp", ".core", ".map"}) {
    const auto out = livehd::shorten_file_name(repeat('x', 300) + ext);
    EXPECT_EQ(livehd::kMaxFileNameBytes, out.size()) << ext;
    EXPECT_TRUE(out.ends_with(ext)) << out;
  }
  // Too long to be an extension: dropped, and the digest is then the tail.
  const auto out = livehd::shorten_file_name(repeat('x', 300) + ".verilog");
  EXPECT_EQ(livehd::kMaxFileNameBytes, out.size());
  EXPECT_FALSE(out.ends_with(".verilog"));
  // A LEADING dot is the whole name's first character, not an extension: the
  // digest stays the tail (the kept prefix still carries that dot).
  const auto dotfile = livehd::shorten_file_name("." + repeat('x', 300));
  EXPECT_EQ(livehd::kMaxFileNameBytes, dotfile.size());
  EXPECT_EQ(std::string::npos, dotfile.substr(dotfile.size() - 64).find('.'));
}

TEST(FileName, PrefixNeverSplitsAUtf8CodePoint) {
  // 'é' is two bytes; a run of them puts a continuation byte at almost every
  // truncation boundary.
  std::string name;
  while (name.size() < 300) {
    name += "\xc3\xa9";
  }
  const auto out = livehd::shorten_file_name(name);
  EXPECT_LE(out.size(), livehd::kMaxFileNameBytes);
  // Every byte of the kept prefix belongs to a complete code point.
  const auto prefix = out.substr(0, out.size() - 65);
  EXPECT_EQ(0U, prefix.size() % 2) << "an 'é' was cut in half";
}

TEST(FileName, UnitStemCollapsesDirectorySeparators) {
  EXPECT_EQ(".._lib_core.core", livehd::unit_file_stem("../lib/core.core"));
  EXPECT_EQ("a_b", livehd::unit_file_stem("a\\b"));
  // A path-qualified unit past the cap keeps its `.core` extension AND stays
  // free of separators, so the emit cannot escape its output directory.
  const auto out = livehd::unit_file_stem("../lib/" + repeat('m', 300) + ".core");
  EXPECT_EQ(livehd::kMaxFileNameBytes, out.size());
  EXPECT_TRUE(out.starts_with(".._lib_mmm")) << out;
  EXPECT_TRUE(out.ends_with(".core")) << out;
  EXPECT_EQ(std::string::npos, out.find('/'));
}
