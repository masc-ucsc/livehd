#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# M3 differential: the native kernel generated straight from the typed LNAST
# (`lhd pass lnast-llvm`, no lgraph cgen) must reproduce, cycle for cycle, what
# the existing `lhd sim` flow (lg -> cgen_sim, llvm backend) shows a Pyrope
# testbench after every `step`. Both sides are driven by the same closed-form
# stimulus (tests/gen_tb.py documents it), so the two TRACE streams are
# compared byte for byte.
set -uo pipefail
LHD="${LHD:-lhd/lhd}"
LHD_ABS="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
RUN="$(cd "$(dirname "pass/lnast_llvm/lnast_llvm_run")" && pwd)/lnast_llvm_run"
SRC="$(cd pass/lnast_llvm/tests && pwd)"
W="${TEST_TMPDIR:-/tmp/lnast_llvm_diff_$$}"
CYCLES=96
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

for design in comb_reg reset_enable reduce_shift struct_port hier async_reset m3_memory m3_memory_array mem_reset mem_reload mem_update mem_collide nary_ops mem_bytes setmask; do
  top="$design"
  d="$W/$design"
  mkdir -p "$d"
  "$LHD_ABS" compile "$SRC/$design.sv" --reader slang --top "$top" --emit-dir "lg:$d/lg" -q >"$d/c.log" 2>&1 \
    || { cat "$d/c.log" >&2; fail "$design: compile to lg failed"; }
  "$LHD_ABS" pass lnast-llvm --top "$top" "lg:$d/lg" --set pass.lg2lnast.passes=balance --emit-dir "sim:$d/k" -q >"$d/g.log" 2>&1 \
    || { cat "$d/g.log" >&2; fail "$design: lnast-llvm failed"; }
  python3 -I "$SRC/gen_tb.py" "$d/k/kernel.json" "$top" "$top" "$CYCLES" >"$d/tb.prp" || fail "$design: testbench generation"
  "$RUN" "$d/k" "$CYCLES" >"$d/new.trace" || fail "$design: kernel run failed"
  (cd "$d" && "$LHD_ABS" sim lg:lg tb.prp --workdir w --set sim.init_zero=true -q >sim.log 2>&1) || { cat "$d/sim.log" >&2; fail "$design: reference sim failed"; }
  "$d/w/sim/drv.bin" --set sim.init_zero=true 2>&1 | grep -a 'puts:TRACE' | sed 's/.*puts://' >"$d/ref.trace"
  [ "$(wc -l <"$d/ref.trace")" -eq "$CYCLES" ] || fail "$design: reference trace has $(wc -l <"$d/ref.trace") lines, want $CYCLES"
  cmp -s "$d/new.trace" "$d/ref.trace" || { diff "$d/new.trace" "$d/ref.trace" | head -10 >&2; fail "$design: traces differ"; }
  echo "ok $design"
done

# Compact loops (Pyrope sources): a `for` with its body inline -- carries, finals, nested loops, and a
# memory read in place (also while this cycle's write to it is visible) -- no call, no array copy.
for design in loop_mem loop_overlay loop_nested; do
  top="$design.$design"
  d="$W/$design"
  mkdir -p "$d"
  "$LHD_ABS" compile "$SRC/$design.prp" --top "$design" --emit-dir "lg:$d/lg" --workdir "$d/tw" -q >"$d/c.log" 2>&1 \
    || { cat "$d/c.log" >&2; fail "$design: compile to lg failed"; }
  "$LHD_ABS" pass lnast-llvm --top "$top" "lg:$d/lg" --emit-dir "sim:$d/k" -q >"$d/g.log" 2>&1 \
    || { cat "$d/g.log" >&2; fail "$design: lnast-llvm failed"; }
  python3 -I "$SRC/gen_tb.py" "$d/k/kernel.json" "$top" "$top" "$CYCLES" 1 >"$d/tb.prp" || fail "$design: testbench generation"
  "$RUN" "$d/k" "$CYCLES" >"$d/new.trace" || fail "$design: kernel run failed"
  (cd "$d" && "$LHD_ABS" sim lg:lg tb.prp --workdir w --set sim.init_zero=true -q >sim.log 2>&1) || { cat "$d/sim.log" >&2; fail "$design: reference sim failed"; }
  "$d/w/sim/drv.bin" --set sim.init_zero=true 2>&1 | grep -a 'puts:TRACE' | sed 's/.*puts://' >"$d/ref.trace"
  [ "$(wc -l <"$d/ref.trace")" -eq "$CYCLES" ] || fail "$design: reference trace has $(wc -l <"$d/ref.trace") lines, want $CYCLES"
  cmp -s "$d/new.trace" "$d/ref.trace" || { diff "$d/new.trace" "$d/ref.trace" | head -10 >&2; fail "$design: traces differ"; }
  echo "ok $design"
done
