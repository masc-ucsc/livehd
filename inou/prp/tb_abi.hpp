//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The run-time ABI of the LLVM testbench flow (todo/livehd/2sl-llvm-only-sim.md), shared by the generator
// (inou/prp/tb_llvm) and the standalone runtime (lhd/lhd_sim_llvm_rt.cpp -> `lhd_simrt`). Header-only: the
// runtime links none of the compiler.

#include <cstdint>

namespace prp_tb {

constexpr unsigned kMaxTbWidth = 16384;

constexpr const char* kObject   = "drv.tb.o";
constexpr const char* kManifest = "drv.tb.json";

// The run-time ABI shared with lhd/lhd_sim_llvm_rt.cpp. A test function is
//   int64_t tb_<n>(Rt* rt, uint64_t* state, const int64_t* args)
// and returns the number of failed asserts.
struct Rt {
  // site: index into the test's `asserts` table of drv.tb.json. `lhs`/`rhs`: the two operands of a top-level
  // comparison as signed two's-complement 64-bit words (the table gives their widths); null for a plain condition.
  void (*assert_fail)(Rt* rt, uint32_t site, int64_t cycle, const uint64_t* lhs, const uint64_t* rhs);
  // site: index into the test's `prints` table; words: each hole's value, whole words in order (the table gives widths)
  void (*print)(Rt* rt, uint32_t site, const uint64_t* words);
  // Called at the start of a top-level tick iteration once `ckpt_left` ran out: the locals are already
  // spilled into `frame`. `stmt` is the top-level statement index of the tick.
  void (*checkpoint)(Rt* rt, uint64_t* state, int64_t cycle, int64_t stmt);
  // Trace object only: before and after stepping all DUT instances.
  void (*trace)(Rt* rt, uint64_t* state, uint32_t before);
  // A literal with `?` bits (the test's `unknowns` table): fills `out` (whole words) with the value drawn for
  // this run -- once per run, never per use.
  void (*unknown)(Rt* rt, uint32_t site, uint64_t* out);
  // A string value (the test's `strings` table): formats it from the hole words and returns its interned handle.
  int64_t (*str)(Rt* rt, uint32_t site, const uint64_t* words);
  // After a DUT step whose edge kernels flagged an undefined access (a division by zero, a memory access outside its
  // array): the instance (index in the manifest) whose flag words (dut.json `undefined`) are non-zero. The runtime
  // reports each site once and clears the words.
  void (*undefined)(Rt* rt, uint64_t* state, uint32_t inst);
  // `std.testplusarg(key)` / `std.valueplusarg(key[, default])` read at run time. `mode`: 0 presence (returns 0/1), 1 integer
  // value, 2 string value (fallback and result are interned string handles), 3 Bool value. `key` is an interned string
  // handle. A bad or missing argument is recorded as the test's error (it is reported when the test ends) and 0 returns.
  int64_t (*plusarg)(Rt* rt, int64_t mode, int64_t key, int64_t fallback, int64_t has_default);
  void*    user;
  int64_t  cycle;        // the current tick iteration (-1 before any tick)
  int64_t  ckpt_left;    // iterations until the next checkpoint poll; the poll re-arms it
  int64_t  resume;       // cycle to resume the tick at, or -1
  int64_t  resume_stmt;  // top-level statement index of the tick being resumed, or -1 (earlier statements are skipped)
  int64_t  stop;         // trace object only: non-zero ends the replay before the next step
  int64_t* frame;        // the test's spilled locals
  int64_t  period;       // number of DUT steps already taken, across all tick blocks
  int64_t  tick_index;   // local clock index, saved and restored at a checkpoint
  uint64_t* resolved_params;  // same word offsets as args, written for expression defaults
};
// Indices of the fields above in 64-bit words (the generated code addresses them that way).
constexpr int         kRtAssert = 0, kRtPrint = 1, kRtCheckpoint = 2, kRtTrace = 3, kRtUnknown = 4, kRtStr = 5, kRtUndefined = 6,
                      kRtPlusarg = 7, kRtUser = 8, kRtCycle = 9, kRtCkptLeft = 10, kRtResume = 11, kRtResumeStmt = 12, kRtStop = 13,
                      kRtFrame = 14, kRtPeriod = 15, kRtTickIndex = 16, kRtResolvedParams = 17;
constexpr const char* kTraceObject = "drv.tb.trace.o";

}  // namespace prp_tb
