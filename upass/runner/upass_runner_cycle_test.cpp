//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "upass_core.hpp"
#include "upass_runner.hpp"

namespace {

struct Test_cycle_a : public upass::uPass {
  using uPass::uPass;
};

struct Test_cycle_b : public upass::uPass {
  using uPass::uPass;
};

struct Test_missing_dep : public upass::uPass {
  using uPass::uPass;
};

static upass::uPass_plugin plugin_cycle_a("__upass_cycle_a", upass::uPass_wrapper<Test_cycle_a>::get_upass, {"__upass_cycle_b"});

static upass::uPass_plugin plugin_cycle_b("__upass_cycle_b", upass::uPass_wrapper<Test_cycle_b>::get_upass, {"__upass_cycle_a"});

static upass::uPass_plugin plugin_missing_dep("__upass_missing_dep", upass::uPass_wrapper<Test_missing_dep>::get_upass,
                                              {"__upass_dep_not_defined"});

class Exposed_runner : public uPass_runner {
public:
  Exposed_runner(std::shared_ptr<upass::Lnast_manager>& _lm, const std::vector<std::string>& names) : uPass_runner(_lm, names) {}

  std::vector<std::string> expose_resolve(const std::vector<std::string>& names) const { return resolve_order(names); }
};

std::shared_ptr<upass::Lnast_manager> make_lm() {
  auto ln = std::make_shared<Lnast>("upass_runner_cycle_test");
  return std::make_shared<upass::Lnast_manager>(ln);
}

}  // namespace

TEST(UpassRunnerResolve, DetectCycle) {
  auto           lm = make_lm();
  Exposed_runner runner(lm, {});

  auto ordered = runner.expose_resolve({"__upass_cycle_a"});
  EXPECT_TRUE(ordered.empty());
}

TEST(UpassRunnerResolve, DetectMissingDependency) {
  auto           lm = make_lm();
  Exposed_runner runner(lm, {});

  auto ordered = runner.expose_resolve({"__upass_missing_dep"});
  EXPECT_TRUE(ordered.empty());
}

TEST(UpassRunnerResolve, SharedNoopResolvesAndRuns) {
  auto           lm = make_lm();
  Exposed_runner runner(lm, {"noop_shared"});

  auto ordered = runner.expose_resolve({"noop_shared"});
  ASSERT_EQ(ordered.size(), 1U);
  EXPECT_EQ(ordered[0], "noop_shared");
  EXPECT_FALSE(runner.has_configuration_error());
}

TEST(UpassRunnerResolve, SharedScanResolves) {
  auto           lm = make_lm();
  Exposed_runner runner(lm, {"scan_shared"});

  auto ordered = runner.expose_resolve({"scan_shared"});
  ASSERT_EQ(ordered.size(), 1U);
  EXPECT_EQ(ordered[0], "scan_shared");
  EXPECT_FALSE(runner.has_configuration_error());
}

TEST(UpassRunnerResolve, SharedDecideResolves) {
  auto           lm = make_lm();
  Exposed_runner runner(lm, {"decide_shared"});

  auto ordered = runner.expose_resolve({"decide_shared"});
  ASSERT_EQ(ordered.size(), 1U);
  EXPECT_EQ(ordered[0], "decide_shared");
  EXPECT_FALSE(runner.has_configuration_error());
}
