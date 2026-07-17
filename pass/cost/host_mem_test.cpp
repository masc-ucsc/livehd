//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// These are the numbers the ABC memory guard refuses on, so the properties that
// matter are the ones a silent unit-mismatch or a wrong syscall would break:
// RSS must be a live value in bytes, and the budget must never exceed physical.

#include "host_mem.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include "gtest/gtest.h"

namespace {
constexpr uint64_t kMiB = 1024 * 1024;

// -c opt is allowed to elide an unobserved new[]/touch sequence entirely
// (heap elision), leaving RSS byte-for-byte unchanged; make the buffer
// observable so the pages really get committed.
inline void keep_alive(void* p) { asm volatile("" : : "g"(p) : "memory"); }
}  // namespace

TEST(HostMem, PhysicalRamIsPlausible) {
  const auto phys = livehd::cost::physical_ram_bytes();
  ASSERT_NE(0U, phys) << "no physical RAM reading: the guard would be unenforceable";
  EXPECT_GT(phys, uint64_t{256} * kMiB) << "implausibly small; a unit error (KiB read as bytes?)";
  EXPECT_LT(phys, uint64_t{64} << 40) << "implausibly large (>=64 TiB): a unit error";
}

TEST(HostMem, RssIsPlausibleAndInBytes) {
  const auto rss = livehd::cost::process_rss_bytes();
  ASSERT_NE(0U, rss);
  // A running gtest binary is megabytes, not kilobytes and not terabytes. This
  // is what catches ru_maxrss's bytes-on-Darwin / KiB-on-Linux trap.
  EXPECT_GT(rss, uint64_t{1} * kMiB);
  EXPECT_LT(rss, livehd::cost::physical_ram_bytes());
}

// The guard samples RSS to decide whether to keep translating, so a reading that
// does not respond to real allocation would let an oversize run through.
TEST(HostMem, RssTracksATouchedAllocation) {
  const auto before = livehd::cost::process_rss_bytes();

  constexpr size_t kBytes = 128 * 1024 * 1024;
  auto             big    = std::make_unique<char[]>(kBytes);
  for (size_t i = 0; i < kBytes; i += 4096) {
    big[i] = static_cast<char>(i);  // touch every page: RSS is resident, not reserved
  }
  keep_alive(big.get());

  const auto after = livehd::cost::process_rss_bytes();
  EXPECT_GT(after, before + (64 * kMiB)) << "RSS did not follow a 128 MiB touched allocation";
}

TEST(HostMem, ReserveIsTheLargerOfTwoGiBAndAFifth) {
  const auto phys    = livehd::cost::physical_ram_bytes();
  const auto reserve = livehd::cost::reserve_bytes();
  EXPECT_EQ(reserve, std::min(std::max<uint64_t>(uint64_t{2} << 30, phys / 5), phys / 2));
}

// The reserve must never eat the whole host: a <=2 GiB machine would otherwise
// get budget 0, which every caller reads as "unenforceable, do not gate" --
// turning the guard OFF on the host most likely to run out of memory.
TEST(HostMem, ReserveNeverConsumesTheWholeHost) {
  const auto phys = livehd::cost::physical_ram_bytes();
  ASSERT_NE(0U, phys);
  EXPECT_LT(livehd::cost::reserve_bytes(), phys) << "reserve >= physical would zero the budget";
  EXPECT_NE(0U, livehd::cost::budget_bytes(0)) << "a real host must always get an enforceable budget";
}

TEST(HostMem, DefaultBudgetLeavesHeadroom) {
  const auto phys   = livehd::cost::physical_ram_bytes();
  const auto budget = livehd::cost::budget_bytes(0);
  ASSERT_NE(0U, budget);
  EXPECT_LT(budget, phys) << "the budget must never span all of physical RAM";
  EXPECT_EQ(budget, phys - livehd::cost::reserve_bytes());
}

TEST(HostMem, ExplicitBudgetWinsAndIsMiB) {
  EXPECT_EQ(uint64_t{4096} << 20, livehd::cost::budget_bytes(4096));
  // An explicit budget is for CI and reproducible hosts, so it is honored even
  // when it is larger than the host -- the caller said so.
  EXPECT_EQ(uint64_t{1} << 20, livehd::cost::budget_bytes(1));
}

// phys_footprint is the jetsam metric: a live byte count that responds to real
// allocation. Threshold kept well below the allocation size because a prior
// test's freed-but-STICKY pages get partially reused (measured), so the growth
// for a fresh 128 MiB buffer can be materially less than 128 MiB.
TEST(HostMem, FootprintIsPlausibleAndTracksAllocation) {
  const auto before = livehd::cost::process_footprint_bytes();
  ASSERT_NE(0U, before);
  EXPECT_GT(before, uint64_t{1} * kMiB);
  EXPECT_LT(before, livehd::cost::physical_ram_bytes());

  constexpr size_t kBytes = 256 * 1024 * 1024;
  auto             big    = std::make_unique<char[]>(kBytes);
  for (size_t i = 0; i < kBytes; i += 4096) {
    big[i] = static_cast<char>(i);
  }
  keep_alive(big.get());
  EXPECT_GT(livehd::cost::process_footprint_bytes(), before + (32 * kMiB))
      << "phys_footprint did not follow a 256 MiB touched allocation";
}

// A zero budget is nothing to enforce: never install a limit. Safe in-process
// (no state change).
TEST(HostMem, ArmZeroBudgetDoesNothing) { EXPECT_EQ(0U, livehd::cost::arm_address_space_limit(0)); }

// Real arming mutates a PROCESS-GLOBAL rlimit, so it is tested in a forked child:
// the parent's limit stays pristine and one test cannot perturb another. This is
// the p5 probe as a regression: arm a small budget, then a large allocation must
// be refused (RLIMIT_AS returns NULL for a big block on both platforms).
TEST(HostMem, ArmedLimitRefusesLargeAllocInChild) {
  const pid_t pid = ::fork();
  ASSERT_GE(pid, 0) << "fork failed";
  if (pid == 0) {
    const uint64_t limit = livehd::cost::arm_address_space_limit(uint64_t{256} << 20);  // 256 MiB headroom
    if (limit == 0) {
      _exit(10);  // arming itself failed -> distinguishable from "not enforced"
    }
    void* p = ::malloc(uint64_t{2} << 30);  // 2 GiB: far past the headroom
    if (p != nullptr) {
      // Touch a page so a lazy mapping actually commits; if this survives, the
      // limit did not enforce.
      *static_cast<volatile char*>(p) = 1;
      _exit(11);
    }
    _exit(42);  // correctly refused
  }
  int status = 0;
  ASSERT_EQ(pid, ::waitpid(pid, &status, 0));
  ASSERT_TRUE(WIFEXITED(status)) << "child died on a signal instead of the clean NULL path";
  EXPECT_EQ(42, WEXITSTATUS(status)) << "armed RLIMIT_AS did not refuse a 2 GiB allocation (10=arm failed, 11=not enforced)";
}

// A lone child needs no share: it already inherits the parent's whole-budget cap,
// and re-arming at budget/1 would be a no-op anyway. Pure predicate, no fork.
TEST(HostMem, ChildShareIsANoOpForASingleChild) {
  EXPECT_EQ(0U, livehd::cost::arm_child_share(1));
  EXPECT_EQ(0U, livehd::cost::arm_child_share(0));
  EXPECT_EQ(0U, livehd::cost::arm_child_share(-1));
}

// THE REGRESSION THAT MATTERS. The whole point of arm_child_share is to TIGHTEN a
// cap the child already inherited -- and arm_address_space_limit deliberately
// refuses to touch an existing cap that is "already at least as strict", so a
// mistake here silently returns 0 and every racer keeps the full budget (the
// N x budget host-killer). Assert the second arming really is lower than the first.
TEST(HostMem, ChildShareTightensTheInheritedLimit) {
  const pid_t pid = ::fork();
  ASSERT_GE(pid, 0) << "fork failed";
  if (pid == 0) {
    ::setenv("LIVEHD_MEMORY_BUDGET_MB", "4096", 1);
    const uint64_t parent_limit = livehd::cost::install_memory_backstop();  // vsize + 4 GiB
    if (parent_limit == 0) {
      _exit(10);  // could not arm at all
    }
    const uint64_t share_limit = livehd::cost::arm_child_share(4);  // vsize + 1 GiB
    if (share_limit == 0) {
      _exit(11);  // refused to re-arm -- the "already strict" guard swallowed it
    }
    if (share_limit >= parent_limit) {
      _exit(12);  // did not tighten
    }
    _exit(42);
  }
  int status = 0;
  ASSERT_EQ(pid, ::waitpid(pid, &status, 0));
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(42, WEXITSTATUS(status)) << "arm_child_share did not tighten the inherited cap "
                                        "(10=parent arm failed, 11=re-arm refused, 12=not tighter)";
}

// End to end: a child armed at its 1/4 share must actually be refused an
// allocation that its share cannot cover but the FULL budget could. This is the
// aggregate guarantee -- 4 racers x 256 MiB stays inside one 1 GiB budget.
TEST(HostMem, ChildShareEnforcesTheDividedBudget) {
  const pid_t pid = ::fork();
  ASSERT_GE(pid, 0) << "fork failed";
  if (pid == 0) {
    ::setenv("LIVEHD_MEMORY_BUDGET_MB", "1024", 1);  // 1 GiB total => 256 MiB per racer
    if (livehd::cost::arm_child_share(4) == 0) {
      _exit(10);
    }
    void* p = ::malloc(uint64_t{512} << 20);  // 512 MiB: fits the budget, NOT the share
    if (p != nullptr) {
      *static_cast<volatile char*>(p) = 1;
      _exit(11);  // the share did not enforce
    }
    _exit(42);
  }
  int status = 0;
  ASSERT_EQ(pid, ::waitpid(pid, &status, 0));
  ASSERT_TRUE(WIFEXITED(status)) << "child died on a signal instead of the clean NULL path";
  EXPECT_EQ(42, WEXITSTATUS(status)) << "a 512 MiB alloc survived a 256 MiB share (10=arm failed, 11=not enforced)";
}

// The startup wrapper resolves a budget from the env and arms; likewise forked so
// it never leaves a limit on the test process.
TEST(HostMem, InstallBackstopArmsFromEnvInChild) {
  const pid_t pid = ::fork();
  ASSERT_GE(pid, 0) << "fork failed";
  if (pid == 0) {
    ::setenv("LIVEHD_MEMORY_BUDGET_MB", "4096", 1);  // 4 GiB headroom
    _exit(livehd::cost::install_memory_backstop() != 0 ? 42 : 11);
  }
  int status = 0;
  ASSERT_EQ(pid, ::waitpid(pid, &status, 0));
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(42, WEXITSTATUS(status)) << "install_memory_backstop did not arm under an explicit env budget";
}
