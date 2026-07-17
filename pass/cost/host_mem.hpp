// This file is distributed under the BSD 3-Clause License. See LICENSE for
// details.
#pragma once

// Host memory facts for ABC memory admission (todo/livehd/2opt-incr.html
// subtask 0): how much RAM this machine really has, and how much this process
// is really using, right now.
//
// PHYSICAL memory only -- never swap, never "available including compressed".
// The failure this guards against is the one that already happened: a
// whole-design XSCore ABC run grew to 221 GB on a 64 GiB host, which the OS
// absorbed by compressing and paging until it killed the process (macOS jetsam,
// exit 137) and dragged the machine down with it. Swap does not make an
// oversize run succeed; it makes it succeed slowly enough to take the host with
// it. So the budget is a fraction of INSTALLED RAM.
//
// NOTE ON THE TWO LAYERS. There is a hard backstop AND a sampled admission gate,
// because neither alone is both safe and diagnosable:
//
//  - HARD BACKSTOP -- arm_address_space_limit() below (setrlimit(RLIMIT_AS)).
//    This guarantees the *host survives* regardless of what allocates, and it is
//    inherited across fork+exec so one call covers every child. But it is not a
//    clean error: ABC never null-checks malloc (thousands of ABC_ALLOC sites),
//    so hitting the cap yields a SIGSEGV -- and on macOS the xzone allocator
//    SIGTRAPs on small allocations rather than returning NULL. A capped crash
//    kills only this process; today's uncapped run drags the whole machine
//    through swap/jetsam. That trade is the entire point of the backstop.
//
//  - SAMPLED ADMISSION -- process_footprint_bytes() + budget_bytes(), sampled by
//    pass/abc while it bit-blasts. This is what produces a *diagnosable* refusal
//    before the backstop ever fires (see over_budget in pass/abc). It is sampled,
//    not predicted, because the ABC mapping peak is not a constant multiple of
//    the translated netlist (measured 2.5x-29.6x; read_lib's fixed cost dominates
//    small regions), so bytes-per-gate cannot be calibrated externally.
//
// CORRECTION (2026-07-15): an earlier version of this comment claimed
// setrlimit(RLIMIT_AS/RLIMIT_DATA/RLIMIT_RSS) "returns EINVAL on Darwin ... the
// only mechanism available is sampling our own RSS." That was wrong. RLIMIT_AS's
// floor on Darwin is the task's *current* virtual_size -- an arm64 process
// reserves ~415 GiB of PROT_NONE VA at startup, so an absolute small limit
// EINVALs for being BELOW the floor, not for being unsupported. Set it to
// (virtual_size + budget) and it enforces to the byte (verified: malloc->NULL,
// mmap->MAP_FAILED, new->bad_alloc, an incremental loop stops at exactly the
// headroom). See arm_address_space_limit().

#include <cstdint>

namespace livehd::cost {

// The memory that can actually kill us: installed physical RAM, or the cgroup
// limit when this process is confined by a smaller one (a container's limit is
// what the OOM killer enforces, not the host's RAM). 0 if it cannot be
// determined -- callers must treat 0 as "no budget can be computed" rather than
// "no memory".
[[nodiscard]] uint64_t physical_ram_bytes();

// This process's CURRENT resident set size (not the peak).
[[nodiscard]] uint64_t process_rss_bytes();

// This process's CURRENT physical footprint -- the metric the OS actually acts
// on when it decides to kill a process (macOS jetsam charges phys_footprint; the
// Linux OOM killer scores on RSS). On Darwin this is TASK_VM_INFO.phys_footprint,
// which counts pages the allocator has compressed and paged out that
// resident_size no longer sees -- i.e. it does not undercount exactly when the
// machine is under the pressure this guard exists for. On Linux it equals
// process_rss_bytes(). 0 if it cannot be determined.
//
// MEASURED: like resident_size, phys_footprint is STICKY after free() -- a 2 GiB
// buffer freed leaves footprint at ~2 GiB until the allocator returns the pages
// (it typically does not; a re-alloc of the same size reuses them). So this is
// the right "am I about to be killed" number, but it is not a live-bytes count.
[[nodiscard]] uint64_t process_footprint_bytes();

// Arm a hard address-space ceiling for THIS process using setrlimit(RLIMIT_AS),
// allowing roughly `budget` bytes of real allocation beyond what the process
// already maps. Returns the RLIMIT_AS value actually installed, or 0 if it could
// not be armed (budget == 0, virtual_size unknown, or setrlimit failed). The
// limit is inherited across fork() AND exec(), so arming once at startup also
// bounds every child (yosys, a re-invoked `lhd sim`, a forked prover).
//
// Darwin: RLIMIT_AS's floor is the task's current virtual_size, so the installed
// value is (virtual_size + budget) read at call time -- never hardcode the
// ~415 GiB baseline, it is arm64- and OS-version-specific. Real allocations grow
// VA ~1:1, so this bounds real memory to ~budget. Linux: the value is `budget`
// absolute (baseline VA is a few MB). This is the HARD BACKSTOP described in the
// file header: it protects the host, it does not produce a clean error.
[[nodiscard]] uint64_t arm_address_space_limit(uint64_t budget);

// The budget this process is configured for: env LIVEHD_MEMORY_BUDGET_MB when
// set, else budget_bytes(0) = physical - reserve. 0 means "unenforceable -- do
// not gate". Reads only the environment and syscalls.
[[nodiscard]] uint64_t configured_budget_bytes();

// Convenience wrapper for process startup: resolve the budget
// (configured_budget_bytes) and arm the RLIMIT_AS backstop. Returns the
// installed limit (0 = not armed). Safe to call before any engine/diag init.
[[nodiscard]] uint64_t install_memory_backstop();

// Re-arm THIS process (a just-forked child) at its 1/nsiblings share of the
// budget. Returns the installed limit, or 0 if nothing was armed.
//
// WHY THIS EXISTS. RLIMIT_AS is PER-PROCESS, and it is inherited across fork --
// so N racing children each inherit a cap allowing the FULL budget. The backstop
// then guarantees nothing in aggregate: N children x budget = N x budget of real
// memory, which is exactly the "many parallel provers took the whole machine
// down" failure the backstop was meant to prevent (pass/lec forks one child per
// proof method, all live at once until the first trustworthy result kills the
// rest). Dividing restores the invariant that the SUM over the process tree
// stays within one budget.
//
// This TIGHTENS only: arm_address_space_limit never raises an existing cap, so a
// child whose inherited VA already grew past its share simply keeps the parent's
// limit. Call it in the child immediately after fork(), before the work starts.
//
// THE TRADE: a child gets budget/N rather than budget, so a solve that genuinely
// needed more now dies (SIGSEGV -- ABC/cvc5 do not null-check malloc) instead of
// taking the host with it. In pass/lec that surfaces as a dead child => a default
// result => Unknown, which is the safe direction for a verdict (never a false
// Proven). Host survival is worth a lost verdict; the reverse is not.
[[nodiscard]] uint64_t arm_child_share(int nsiblings);

// Headroom left to the OS and the rest of the machine: max(2 GiB, 20% of
// physical), capped at half of physical so a small host still gets a usable
// budget. Not tunable -- `budget_mb` below is the tunable.
[[nodiscard]] uint64_t reserve_bytes();

// The ceiling a single ABC region may reach, as TOTAL process RSS.
// `budget_mb > 0` pins it explicitly (reproducible hosts and CI); otherwise it
// is physical - reserve. Returns 0 when physical RAM is unknown and no explicit
// budget was given, meaning "unenforceable -- do not gate".
[[nodiscard]] uint64_t budget_bytes(int budget_mb);

}  // namespace livehd::cost
