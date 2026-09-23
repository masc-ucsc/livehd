// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_worker.hpp"

#include <signal.h>
#include <sys/wait.h>

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "abc_tmap.hpp"
#include "gtest/gtest.h"
#include "mapping_wire.hpp"

namespace livehd::synth {
namespace {
Mapping_request request() {
  Mapping_request r;
  r.library = "inou/prp/tests/abc/test.lib";
  r.inputs  = {
      {"a", {}, 0},
      {"b", {}, 0}
  };
  r.terms = {
      {0, 1}
  };
  r.remaining_ms = [] { return 5000; };
  return r;
}
TEST(AbcWorker, RealSelfExecMatchesDirectMappingAndCanRunAgain) {
  Abc_tmap direct, isolated("lhd/lhd");
  auto     r = request();
  for (bool inverter : {false, true}) {
    if (inverter) {
      r.inputs.resize(1);
      r.terms.clear();
      r.inverter = true;
    }
    auto normal         = r;
    normal.remaining_ms = {};
    const auto expected = direct.map(normal);
    ASSERT_EQ(expected.status, Map_status::mapped);
    const auto mapped = isolated.map(r);
    ASSERT_EQ(mapped.status, Map_status::mapped) << mapped.reason;
    EXPECT_EQ(wire::encode_fragment(mapped), wire::encode_fragment(expected));
  }
  auto timed                 = request();
  timed.library              = "inou/prp/tests/abc/timing.lib";
  timed.inputs[0].arrival_ps = 10;
  timed.required_ps          = 120;
  timed.output_load_ff       = 4;
  auto timed_direct          = timed;
  timed_direct.remaining_ms  = {};
  const auto expected_timing = direct.map(timed_direct);
  const auto actual_timing   = isolated.map(timed);
  ASSERT_EQ(expected_timing.status, Map_status::mapped) << expected_timing.reason;
  ASSERT_EQ(actual_timing.status, Map_status::mapped) << actual_timing.reason;
  EXPECT_EQ(wire::encode_fragment(actual_timing), wire::encode_fragment(expected_timing));
  EXPECT_EQ(isolated.worker_statistics().calls, 3);
  EXPECT_EQ(isolated.worker_statistics().failures, 0);
  r.remaining_ms = [] { return 0; };
  EXPECT_EQ(isolated.map(r).status, Map_status::exhausted);
  EXPECT_EQ(isolated.worker_statistics().stopped, 1);
  r.remaining_ms = [] { return 5000; };
  EXPECT_EQ(isolated.map(r).status, Map_status::mapped);
}
TEST(AbcWorker, DeadlineAndCancellationKillAndReapOnlyTheOwnedWorker) {
  auto pattern = (std::filesystem::temp_directory_path() / "livehd-worker-test-XXXXXX").string();
  ASSERT_NE(mkdtemp(pattern.data()), nullptr);
  const std::filesystem::path directory(pattern);
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::filesystem::remove_all(path); }
  } cleanup{directory};
  const auto script  = directory / "worker";
  const auto pidfile = directory / "pid";
  std::ofstream(script) << "#!/bin/sh\nprintf '%s' \"$$\" > \"${0%/*}/pid\"\nexec /bin/sleep 10\n";
  std::filesystem::permissions(script, std::filesystem::perms::owner_all);
  const auto worker_ready = [&] {
    int pid = -1;
    std::ifstream(pidfile) >> pid;
    // Opening the redirected file happens before printf writes the PID. Do
    // not cancel in that interval: the reaping check needs the actual PID.
    return pid > 0;
  };
  for (unsigned mode : {0, 1, 2}) {
    SCOPED_TRACE(mode);
    const bool cancellation = mode != 0;
    std::filesystem::remove(pidfile);
    auto r         = request();
    r.remaining_ms = [=] { return cancellation ? 5000 : 200; };
    if (mode == 1) {
      r.admission = [&] { return !worker_ready(); };
    }
    uint64_t peak = 0;
    if (mode == 2) {
      r.worker_admission = [&](uint64_t bytes) {
        peak = std::max(peak, bytes);
        return !worker_ready();
      };
    }
    const auto start  = std::chrono::steady_clock::now();
    const auto result = map_in_worker(script.string(), r);
    EXPECT_EQ(result.fragment.status, Map_status::exhausted);
    EXPECT_TRUE(result.stopped);
    EXPECT_FALSE(result.failed);
    if (mode == 2) {
      EXPECT_GT(peak, 0);
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    EXPECT_LT(elapsed, 2);
    if (!cancellation) {
      EXPECT_GE(elapsed, 0.2);
      // Startup is part of the deadline. If it expires before spawn there is
      // no child to reap; the cancellation cases below require a live child.
      if (result.worker_pid == 0) {
        continue;
      }
    }
    ASSERT_GT(result.worker_pid, 0);
    const auto pid = static_cast<pid_t>(result.worker_pid);
    if (cancellation) {
      int reported = -1;
      std::ifstream(pidfile) >> reported;
      ASSERT_EQ(reported, pid);
    }
    int status = 0;
    EXPECT_EQ(waitpid(pid, &status, WNOHANG), -1);
    EXPECT_EQ(errno, ECHILD);
    EXPECT_EQ(kill(pid, 0), -1);
    EXPECT_EQ(errno, ESRCH);
  }
  auto missing = map_in_worker((directory / "missing").string(), request());
  EXPECT_TRUE(missing.failed);
  EXPECT_FALSE(missing.stopped);
  auto malformed = map_in_worker("/usr/bin/true", request());
  EXPECT_TRUE(malformed.failed);
  EXPECT_EQ(malformed.fragment.status, Map_status::exhausted);
}
TEST(AbcWorker, RequestWirePreservesTimingAndRejectsMalformedInput) {
  auto r                   = request();
  r.inputs[0].arrival_ps   = 12.5;
  r.inputs[0].driving_cell = "BUF";
  r.required_ps            = 500;
  r.output_load_ff         = 4;
  const auto encoded       = wire::encode_request(r);
  const auto decoded       = wire::decode_request(encoded);
  EXPECT_EQ(wire::encode_request(decoded), encoded);
  EXPECT_FALSE(decoded.admission);
  EXPECT_FALSE(decoded.remaining_ms);
  EXPECT_FALSE(decoded.worker_admission);
  EXPECT_THROW(wire::decode_request(encoded + "x"), std::runtime_error);
  EXPECT_THROW(wire::decode_request(encoded.substr(0, encoded.size() - 1)), std::runtime_error);
  r.inputs.resize(25);
  EXPECT_THROW(wire::decode_request(wire::encode_request(r)), std::runtime_error);
}
}  // namespace
}  // namespace livehd::synth
