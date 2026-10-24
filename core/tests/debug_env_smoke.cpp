// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "debug_env.hpp"

namespace {
// Restore the caller's environment even when an assertion fails.
struct Env_guard {
  const char*                name;
  std::optional<std::string> previous;
  explicit Env_guard(const char* key) : name(key) {
    if (const char* value = std::getenv(key)) {
      previous = value;
    }
  }
  ~Env_guard() {
    if (previous) {
      ::setenv(name, previous->c_str(), 1);
    } else {
      ::unsetenv(name);
    }
  }
};

const char* other_call_site() { return livehd::debug::env<"LHD_ENV_SMOKE_VALUE">(); }
}  // namespace

TEST(DebugEnv, OwnsValueAndSharesSnapshotAcrossCallSites) {
  Env_guard guard("LHD_ENV_SMOKE_VALUE");
  ASSERT_EQ(::setenv(guard.name, "original", 1), 0);
  const char* first = livehd::debug::env<"LHD_ENV_SMOKE_VALUE">();
  ASSERT_STREQ(first, "original");
  ASSERT_EQ(::setenv(guard.name, "replacement", 1), 0);
  EXPECT_EQ(other_call_site(), first);
  EXPECT_STREQ(other_call_site(), "original");
  ASSERT_EQ(::unsetenv(guard.name), 0);
  EXPECT_STREQ(other_call_site(), "original");
}

TEST(DebugEnv, CachesAbsence) {
  Env_guard guard("LHD_ENV_SMOKE_ABSENT");
  ASSERT_EQ(::unsetenv(guard.name), 0);
  EXPECT_EQ(livehd::debug::env<"LHD_ENV_SMOKE_ABSENT">(), nullptr);
  ASSERT_EQ(::setenv(guard.name, "later", 1), 0);
  EXPECT_EQ(livehd::debug::env<"LHD_ENV_SMOKE_ABSENT">(), nullptr);
}

TEST(DebugEnv, PreservesEmptyValue) {
  Env_guard guard("LHD_ENV_SMOKE_EMPTY");
  ASSERT_EQ(::setenv(guard.name, "", 1), 0);
  EXPECT_STREQ(livehd::debug::env<"LHD_ENV_SMOKE_EMPTY">(), "");
}
