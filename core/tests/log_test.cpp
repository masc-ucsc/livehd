//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "log.hpp"

#include "gtest/gtest.h"

using livehd::log::configure;
using livehd::log::enabled;
using livehd::log::id;
using livehd::log::is_channel;
using livehd::log::Level;
using livehd::log::parse_level;
using livehd::log::reset;

namespace {

class Log_test : public ::testing::Test {
protected:
  void        SetUp() override { reset(); }  // each test starts from all-off
  static bool on(std::string_view ch, Level lvl) { return enabled(id(ch), lvl); }
};

TEST_F(Log_test, parse_level_vocabulary) {
  bool ok = false;
  EXPECT_EQ(parse_level("off", ok), Level::off);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parse_level("error", ok), Level::error);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parse_level("warn", ok), Level::warn);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parse_level("warning", ok), Level::warn);  // alias
  EXPECT_TRUE(ok);
  EXPECT_EQ(parse_level("info", ok), Level::info);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parse_level("debug", ok), Level::debug);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parse_level("trace", ok), Level::trace);
  EXPECT_TRUE(ok);

  parse_level("bogus", ok);
  EXPECT_FALSE(ok);
  parse_level("", ok);
  EXPECT_FALSE(ok);
  parse_level("INFO", ok);  // case-sensitive
  EXPECT_FALSE(ok);
}

TEST_F(Log_test, is_channel_known_and_unknown) {
  EXPECT_TRUE(is_channel("upass"));
  EXPECT_TRUE(is_channel("upass.verifier"));
  EXPECT_TRUE(is_channel("pass.abc"));
  EXPECT_TRUE(is_channel("core"));
  EXPECT_TRUE(is_channel("tolg"));
  EXPECT_FALSE(is_channel("nope"));
  EXPECT_FALSE(is_channel("upass.nope"));
  EXPECT_FALSE(is_channel("upass.tolg"));  // renamed to the standalone "tolg"
  EXPECT_FALSE(is_channel(""));
}

TEST_F(Log_test, off_by_default) {
  EXPECT_FALSE(on("upass", Level::error));
  EXPECT_FALSE(on("upass.verifier", Level::error));
  EXPECT_FALSE(on("pass", Level::trace));
}

TEST_F(Log_test, level_threshold) {
  configure("upass", Level::info);
  EXPECT_TRUE(on("upass", Level::error));  // error/warn/info <= info
  EXPECT_TRUE(on("upass", Level::warn));
  EXPECT_TRUE(on("upass", Level::info));
  EXPECT_FALSE(on("upass", Level::debug));  // debug/trace > info
  EXPECT_FALSE(on("upass", Level::trace));
}

TEST_F(Log_test, subtree_inheritance) {
  configure("upass", Level::info);
  EXPECT_TRUE(on("upass.verifier", Level::info));   // child inherits the parent level
  EXPECT_TRUE(on("upass.bitwidth", Level::warn));
  EXPECT_FALSE(on("upass.verifier", Level::debug));  // inherited level still bounds the child
  EXPECT_FALSE(on("cprop", Level::error));           // unrelated channel unaffected
  EXPECT_FALSE(on("tolg", Level::error));            // "tolg" is NOT under the "upass" subtree
}

TEST_F(Log_test, child_override_wins_parent_unchanged) {
  configure("upass", Level::info);
  configure("upass.verifier", Level::debug);
  EXPECT_TRUE(on("upass.verifier", Level::debug));   // more-specific child wins
  EXPECT_FALSE(on("upass", Level::debug));            // parent keeps its own level
  EXPECT_TRUE(on("upass", Level::info));
  EXPECT_FALSE(on("upass.bitwidth", Level::debug));   // sibling still on the parent level
  EXPECT_TRUE(on("upass.bitwidth", Level::info));
}

TEST_F(Log_test, child_does_not_leak_up) {
  configure("upass.verifier", Level::trace);
  EXPECT_TRUE(on("upass.verifier", Level::trace));
  EXPECT_FALSE(on("upass", Level::error));  // configuring a child must NOT enable the parent
}

TEST_F(Log_test, last_configure_wins) {
  configure("upass", Level::info);
  configure("upass", Level::error);
  EXPECT_TRUE(on("upass", Level::error));
  EXPECT_FALSE(on("upass", Level::info));
}

TEST_F(Log_test, off_disables_again) {
  configure("upass", Level::trace);
  EXPECT_TRUE(on("upass", Level::trace));
  configure("upass", Level::off);
  EXPECT_FALSE(on("upass", Level::error));
}

TEST_F(Log_test, reset_clears) {
  configure("upass", Level::trace);
  reset();
  EXPECT_FALSE(on("upass", Level::error));
}

}  // namespace
