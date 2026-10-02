// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// fork_race wall deadline. cvc5's tlimit-per bounds a checkSat but not the
// preprocessing a push() runs, so a racer stuck there never returned and the
// parent's poll(-1) turned formal.timeout=60 into a 15+ minute `lhd lec`
// (lhdtrack br_fifo_shared_dynamic_flops_push_credit_pop_credit, asap7). These
// cases pin that the parent now kills a racer past its deadline, reaps it (no
// zombie), reports it timed_out with NO result (never a verdict), and that a
// normal race is untouched. A killed racer takes its WHOLE subtree with it (the
// ABC cone-pass grandchild a racer forks has no clock of its own), and racers
// do not outlive a parent that is killed alone.

#include "fork_race.hpp"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <string>
#include <string_view>

#include "gtest/gtest.h"

namespace {

using livehd::lec::fork_race;
using livehd::lec::race_deadline_ms;

// The wire codec of a trivial R: the payload is the int in decimal.
std::string ser(const int& v) { return std::to_string(v); }
bool        deser(std::string_view b, int& v) {
  if (b.empty()) {
    return false;
  }
  v = std::stoi(std::string(b));
  return true;
}

long long ms_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

// No child of this process may be left behind: waitpid(-1) must report ECHILD.
void expect_no_children() {
  int         st = 0;
  const pid_t p  = ::waitpid(-1, &st, WNOHANG);
  EXPECT_EQ(p, -1) << "a racer was left unreaped (pid " << p << ")";
  EXPECT_EQ(errno, ECHILD);
}

TEST(ForkRace, StalledRacersAreKilledAtTheDeadline) {
  // Both racers stall far past the 2 s deadline (the stuck-in-preprocessing
  // shape). The race must return in about the deadline, not after 30 s.
  auto run = [](int i) -> int {
    ::sleep(30);
    return 100 + i;
  };
  auto       trust = [](int, const int& r) { return r == 101; };
  auto       t0    = std::chrono::steady_clock::now();
  auto       race  = fork_race<int>(2, run, ser, deser, trust, 2000);
  const auto took  = ms_since(t0);
  ASSERT_TRUE(race.forked);
  EXPECT_LT(took, 6000) << "fork_race ignored its 2 s wall deadline";
  EXPECT_GE(took, 1900);
  EXPECT_TRUE(race.deadline_hit);
  EXPECT_EQ(race.winner, -1);
  for (int i = 0; i < 2; ++i) {
    EXPECT_TRUE(race.timed_out[i]);
    EXPECT_FALSE(race.done[i]);
    EXPECT_EQ(race.results[i], 0) << "a killed racer must carry NO result";
  }
  expect_no_children();
}

TEST(ForkRace, FinishedRacerKeptStalledRacerKilled) {
  // Racer 0 answers quickly but is not trustworthy, so the race keeps waiting
  // for racer 1 -- which is stuck. Racer 0's result survives; racer 1 is killed.
  auto run = [](int i) -> int {
    if (i == 1) {
      ::sleep(30);
    }
    return 7 + i;
  };
  auto trust = [](int, const int&) { return false; };
  auto t0    = std::chrono::steady_clock::now();
  auto race  = fork_race<int>(2, run, ser, deser, trust, 2000);
  EXPECT_LT(ms_since(t0), 6000);
  EXPECT_TRUE(race.deadline_hit);
  EXPECT_TRUE(race.done[0]);
  EXPECT_FALSE(race.timed_out[0]);
  EXPECT_EQ(race.results[0], 7);
  EXPECT_TRUE(race.timed_out[1]);
  EXPECT_FALSE(race.done[1]);
  EXPECT_EQ(race.results[1], 0);
  expect_no_children();
}

TEST(ForkRace, TrustedWinnerCancelsStalledLoserWithoutDeadline) {
  // The early-cancel path is unchanged: a trusted winner kills the stuck loser
  // long before the (generous) deadline, and that is not a timeout.
  auto run = [](int i) -> int {
    if (i == 0) {
      ::sleep(30);
    }
    return 40 + i;
  };
  auto trust = [](int i, const int& r) { return i == 1 && r == 41; };
  auto t0    = std::chrono::steady_clock::now();
  auto race  = fork_race<int>(2, run, ser, deser, trust, 60000);
  EXPECT_LT(ms_since(t0), 5000);
  EXPECT_EQ(race.winner, 1);
  EXPECT_FALSE(race.deadline_hit);
  EXPECT_FALSE(race.timed_out[0]);
  EXPECT_EQ(race.results[1], 41);
  expect_no_children();
}

TEST(ForkRace, NormalRaceUnderDeadlineCollectsEveryResult) {
  auto run   = [](int i) -> int { return 10 * (i + 1); };
  auto trust = [](int, const int&) { return false; };
  auto race  = fork_race<int>(3, run, ser, deser, trust, 30000);
  EXPECT_FALSE(race.deadline_hit);
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(race.done[i]);
    EXPECT_FALSE(race.timed_out[i]);
    EXPECT_EQ(race.results[i], 10 * (i + 1));
  }
  expect_no_children();
}

// True once `p` no longer exists (killed and reaped by whoever adopted it).
bool gone_within(pid_t p, int ms) {
  const auto t0 = std::chrono::steady_clock::now();
  while (ms_since(t0) < ms) {
    if (::kill(p, 0) != 0 && errno == ESRCH) {
      return true;
    }
    ::usleep(20000);
  }
  return false;
}

pid_t read_pid(int fd) {
  pid_t p = -1;
  if (::read(fd, &p, sizeof p) != static_cast<ssize_t>(sizeof p)) {
    return -1;
  }
  return p;
}

TEST(ForkRace, DeadlineKillTakesTheRacersGrandchildren) {
  // Each racer forks a helper that runs on its own (the ABC cone-pass child
  // shape: only the racer enforces its clocks) and reports the helper's pid.
  // Killing just the racer pid left the helper reparented to init, burning a
  // core for minutes. The deadline kill must take the racer's whole subtree.
  int p[2];
  ASSERT_EQ(::pipe(p), 0);
  auto run = [&p](int i) -> int {
    const pid_t g = ::fork();
    if (g == 0) {
      const pid_t me = ::getpid();
      if (::write(p[1], &me, sizeof me) != static_cast<ssize_t>(sizeof me)) {
        ::_exit(1);
      }
      ::sleep(30);
      ::_exit(0);
    }
    ::sleep(30);
    return 100 + i;
  };
  auto trust = [](int, const int&) { return false; };
  auto race  = fork_race<int>(2, run, ser, deser, trust, 1500);
  ::close(p[1]);
  EXPECT_TRUE(race.deadline_hit);
  for (int i = 0; i < 2; ++i) {
    const pid_t g = read_pid(p[0]);
    ASSERT_GT(g, 0) << "racer " << i << " did not report its helper";
    EXPECT_TRUE(gone_within(g, 3000)) << "helper " << g << " outlived its killed racer (orphan)";
    ::kill(g, SIGKILL);  // never leak it past the test, whatever the verdict
  }
  ::close(p[0]);
  expect_no_children();
}

TEST(ForkRace, RacersDieWithTheirParent) {
  // An outer watchdog that kills only the lhd process (or Ctrl-C reaching it)
  // must not leave racers running: they exit once their parent is gone.
  int p[2];
  ASSERT_EQ(::pipe(p), 0);
  const pid_t driver = ::fork();
  ASSERT_GE(driver, 0);
  if (driver == 0) {
    ::close(p[0]);
    auto run = [&p](int i) -> int {
      const pid_t me = ::getpid();
      if (::write(p[1], &me, sizeof me) != static_cast<ssize_t>(sizeof me)) {
        ::_exit(1);
      }
      ::sleep(30);
      return i;
    };
    auto trust = [](int, const int&) { return false; };
    (void)fork_race<int>(2, run, ser, deser, trust, 60000);
    ::_exit(0);
  }
  ::close(p[1]);
  const pid_t r0 = read_pid(p[0]);
  const pid_t r1 = read_pid(p[0]);
  ::close(p[0]);
  ASSERT_GT(r0, 0);
  ASSERT_GT(r1, 0);
  ::kill(driver, SIGKILL);
  int st = 0;
  while (::waitpid(driver, &st, 0) < 0 && errno == EINTR) {
  }
  EXPECT_TRUE(gone_within(r0, 3000)) << "racer " << r0 << " outlived its killed parent";
  EXPECT_TRUE(gone_within(r1, 3000)) << "racer " << r1 << " outlived its killed parent";
  ::kill(r0, SIGKILL);
  ::kill(r1, SIGKILL);
  expect_no_children();
}

TEST(ForkRace, DeadlineFormula) {
  livehd::lec::Lec_options o;
  o.timeout = 0;
  EXPECT_EQ(race_deadline_ms(o), 0) << "formal.timeout=0 is unbounded";
  o.timeout           = 60;
  o.min_timeout       = 20;
  o.hard_timeout_mult = 0;
  EXPECT_EQ(race_deadline_ms(o), 0) << "hard_timeout_mult=0 disables every wall backstop";
  o.hard_timeout_mult = 3;
  EXPECT_EQ(race_deadline_ms(o), (60 + 20 + 15) * 1000LL);
  o.timeout     = 2;
  o.min_timeout = 1;
  EXPECT_EQ(race_deadline_ms(o), (2 + 1 + 10) * 1000LL) << "grace floor is 10 s";
  o.timeout     = 300;
  o.min_timeout = 20;
  EXPECT_EQ(race_deadline_ms(o), (300 + 20 + 60) * 1000LL) << "grace cap is 60 s";
}

// The verify strategy race may legitimately run to formal.timeout +
// (unsettled units x formal.min_timeout) (Lec_options::min_timeout). Its wall
// deadline must never undercut that: a flat floor killed ind-first at 14 s on a
// 24-obligation design whose documented bound was 1 + 24x3 = 73 s, throwing
// away every unbounded induction proof.
TEST(ForkRace, VerifyDeadlineScalesWithUnits) {
  using livehd::lec::verify_race_deadline_ms;
  livehd::lec::Lec_options o;
  o.timeout = 0;
  EXPECT_EQ(verify_race_deadline_ms(o, 24), 0) << "formal.timeout=0 is unbounded";
  o.timeout           = 1;
  o.min_timeout       = 3;
  o.hard_timeout_mult = 0;
  EXPECT_EQ(verify_race_deadline_ms(o, 24), 0) << "hard_timeout_mult=0 disables every wall backstop";
  o.hard_timeout_mult = 3;
  // timeout + units x min_timeout + grace (10 s floor).
  EXPECT_EQ(verify_race_deadline_ms(o, 24), (1 + 24 * 3 + 10) * 1000LL);
  EXPECT_GE(verify_race_deadline_ms(o, 24), (1 + 24 * 3) * 1000LL) << "never under the documented per-unit bound";
  // A unit-free design still keeps one floor (race_deadline_ms) ...
  EXPECT_EQ(verify_race_deadline_ms(o, 0), race_deadline_ms(o));
  // ... and never drops under the isolated worker's timeout x mult backstop.
  o.timeout     = 60;
  o.min_timeout = 1;
  EXPECT_EQ(verify_race_deadline_ms(o, 2), 180 * 1000LL);
  o.min_timeout = 20;
  EXPECT_EQ(verify_race_deadline_ms(o, 24), (60 + 24 * 20 + 15) * 1000LL);
  // phase=full runs just_reset and after_reset back to back in one strategy.
  o.phase = "full";
  EXPECT_EQ(verify_race_deadline_ms(o, 24), (2 * (60 + 24 * 20) + 15) * 1000LL);
}

}  // namespace
