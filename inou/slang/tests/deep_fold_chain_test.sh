#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The Pyrope writer runs its files on worker threads, and a default
# secondary-thread stack is 512 KiB on macOS (main() gets 8 MiB). Two of the
# writer's recursions walk a chain of dependent single-use temps one frame per
# temp, so their depth is set by the DESIGN:
#
#   * render_def_rhs <-> render_value -- inlining the chain into one nested
#     expression. At -O0 the pre-split render_def_rhs frame was 13.8 KiB; 34
#     levels overflowed the worker. That is how `lhd compile --top cva6
#     --emit-dir pyrope:` died with `Bus error: 10` on every non-opt build
#     (2026-08-21): perf_counters' packed-array writes lower to exactly such a
#     shl/or/sext temp chain.
#   * summarize_stability_shape -- the fold-eligibility analysis, ~0.5 KiB per
#     temp at -O0. MEASURED: the pre-fix fastbuild binary dies in it at a
#     500-deep chain (1,069 frames) and survives 300.
#
# The fix is livehd::run_workers (core/worker_pool.hpp): every worker pool in
# the tree gets a 64 MiB stack. This gate drives both recursions through a
# generated 500-deep `((... << 1) | x[i])` nest -- generated, not checked in.
# 500 and not more: slang's own constant evaluator recurses over the SOURCE
# nest on the main thread and a 1000-deep one overflows 8 MiB inside slang
# (CVA6's chain came from flat statements, not from source nesting). Both legs
# must pass -- the emit (the leg that crashed) AND the recompile of what was
# emitted.

set -u

LHD=lhd/lhd
TOP=deep_fold_chain
DEPTH=500
W="${TEST_TMPDIR:-/tmp/deep_fold_chain_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

SRC="$W/$TOP.v"
{
  echo "module $TOP ("
  echo "  input  logic [$((DEPTH - 1)):0] x,"
  echo "  output logic [63:0] y"
  echo ");"
  printf '  assign y = '
  awk -v n="$DEPTH" 'BEGIN {
    e = "x[0]";
    for (i = 1; i < n; i++) e = "((" e " << 1) | x[" i "])";
    print e ";";
  }'
  echo "endmodule"
} > "$SRC"

# ── leg 1: the emit that used to die at a stack guard page ──────────────────
out=$("$LHD" compile "$SRC" --reader slang --top "$TOP" \
  --emit-dir pyrope:"$W/prp" --workdir "$W/work" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "verilog -> pyrope exited $rc (138 = bus error / stack overflow): $out"

prp="$W/prp/$TOP.prp"
[ -s "$prp" ] || fail "Pyrope unit was not emitted"

# The gate is only a gate while the chain reaches the writer as ONE chain of
# dependent temps. Every `<< 1` of the source must survive into the emit (the
# writer keeps the chain; how many levels it inlines per statement is its own
# business and is NOT what this test pins).
shifts=$(grep -o '<< 1' "$prp" | wc -l | tr -d ' ')
[ "$shifts" -ge $((DEPTH - 1)) ] || fail "expected >= $((DEPTH - 1)) shifts in the emit, found $shifts -- the chain no longer reaches the writer, so this test no longer exercises its recursions"

# ── leg 2: the emitted (equally deep) Pyrope recompiles ─────────────────────
out=$("$LHD" compile "$prp" --emit-dir lg:"$W/lg" --workdir "$W/rt" 2>&1) \
  || fail "emitted Pyrope does not recompile: $out"

echo "PASS: a $DEPTH-deep dependent temp chain emits and recompiles"
