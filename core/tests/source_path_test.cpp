// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "source_path.hpp"

#include "gtest/gtest.h"

// A generated-source directory with a random name (memory lowering's mkdtemp scratch) must still give its files the
// same recorded path in every run, so SourceIds and emitted source maps are reproducible.
TEST(SourcePath, AliasedGeneratedDirectoryIsLocationIndependent) {
  const std::string a = "/tmp/lhd-memory-AbC123";
  const std::string b = "/scratch/other/lhd-memory-zz9QpL";
  livehd::srcloc::register_source_alias(a, "lhd-memory");
  livehd::srcloc::register_source_alias(b + "/", "lhd-memory");
  EXPECT_EQ(livehd::srcloc::workspace_relative(a + "/m/m.v"), "lhd-memory/m/m.v");
  EXPECT_EQ(livehd::srcloc::workspace_relative(b + "/m/m.v"), "lhd-memory/m/m.v");
  // A front end may spell the file relative to the current directory (slang under yosys does).
  const auto cwd   = std::filesystem::current_path();
  const auto inner = (std::filesystem::path(a) / "m" / "m.v").lexically_relative(cwd).string();
  EXPECT_EQ(livehd::srcloc::workspace_relative(inner), "lhd-memory/m/m.v");
  // A sibling whose name only starts with the prefix is not inside the aliased directory.
  EXPECT_EQ(livehd::srcloc::workspace_relative(a + "X/m.v"), "tmp/lhd-memory-AbC123X/m.v");
  livehd::srcloc::unregister_source_alias(a);
  livehd::srcloc::unregister_source_alias(b);
  EXPECT_EQ(livehd::srcloc::workspace_relative(a + "/m/m.v"), "tmp/lhd-memory-AbC123/m/m.v");
}
