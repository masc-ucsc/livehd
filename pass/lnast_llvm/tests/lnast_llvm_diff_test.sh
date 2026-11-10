#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# M3 differential: the native kernels generated straight from the typed LNAST
# (`lhd pass lnast-llvm`, no lgraph cgen) must reproduce, cycle for cycle, what
# the existing `lhd sim` flow (lg -> cgen_sim, llvm backend) shows a Pyrope
# testbench after every `step`. Both sides are driven by the same closed-form
# stimulus (tests/gen_tb.py documents it), so the two TRACE streams are
# compared byte for byte.
#
# Each design is independent (its own directory), and most of a design's time is
# the reference simulator's host compile, so the designs run a few at a time.
set -uo pipefail
LHD="${LHD:-lhd/lhd}"
LHD_ABS="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
RUN="$(cd "$(dirname "pass/lnast_llvm/lnast_llvm_run")" && pwd)/lnast_llvm_run"
SRC="$(cd pass/lnast_llvm/tests && pwd)"
W="${TEST_TMPDIR:-/tmp/lnast_llvm_diff_$$}"
CYCLES=96
JOBS=6
mkdir -p "$W"

# one_design NAME KIND: KIND is `sv` (SystemVerilog source) or `prp` (Pyrope source; these are also
# run with every loop kept rolled). Prints `ok NAME` and returns 0, or a FAIL line and returns 1.
one_design() {
  local design=$1 kind=$2 top d
  d="$W/$design"
  mkdir -p "$d"
  fail() { echo "FAIL: $*" >&2; return 1; }
  if [ "$kind" = sv ]; then
    top="$design"
    "$LHD_ABS" compile "$SRC/$design.sv" --reader slang --top "$top" --emit-dir "lg:$d/lg" -q >"$d/c.log" 2>&1 \
      || { cat "$d/c.log" >&2; fail "$design: compile to lg failed"; return 1; }
    "$LHD_ABS" pass lnast-llvm --top "$top" "lg:$d/lg" --set pass.lg2lnast.passes=balance --emit-dir "sim:$d/k" -q >"$d/g.log" 2>&1 \
      || { cat "$d/g.log" >&2; fail "$design: lnast-llvm failed"; return 1; }
    python3 -I "$SRC/gen_tb.py" "$d/k/kernel.json" "$top" "$top" "$CYCLES" >"$d/tb.prp" || { fail "$design: testbench generation"; return 1; }
  else
    top="$design.$design"
    "$LHD_ABS" compile "$SRC/$design.prp" --top "$design" --emit-dir "lg:$d/lg" --workdir "$d/tw" -q >"$d/c.log" 2>&1 \
      || { cat "$d/c.log" >&2; fail "$design: compile to lg failed"; return 1; }
    "$LHD_ABS" pass lnast-llvm --top "$top" "lg:$d/lg" --emit-dir "sim:$d/k" -q >"$d/g.log" 2>&1 \
      || { cat "$d/g.log" >&2; fail "$design: lnast-llvm failed"; return 1; }
    python3 -I "$SRC/gen_tb.py" "$d/k/kernel.json" "$top" "$top" "$CYCLES" 1 >"$d/tb.prp" || { fail "$design: testbench generation"; return 1; }
  fi
  "$RUN" "$d/k" "$CYCLES" >"$d/new.trace" || { fail "$design: kernel run failed"; return 1; }
  (cd "$d" && "$LHD_ABS" sim lg:lg tb.prp --workdir w --set sim.init_zero=true -q >sim.log 2>&1) \
    || { cat "$d/sim.log" >&2; fail "$design: reference sim failed"; return 1; }
  "$d/w/sim/drv.bin" --set sim.init_zero=true 2>&1 | grep -a 'puts:TRACE' | sed 's/.*puts://' >"$d/ref.trace"
  [ "$(wc -l <"$d/ref.trace")" -eq "$CYCLES" ] || { fail "$design: reference trace has $(wc -l <"$d/ref.trace") lines, want $CYCLES"; return 1; }
  cmp -s "$d/new.trace" "$d/ref.trace" || { diff "$d/new.trace" "$d/ref.trace" | head -10 >&2; fail "$design: traces differ"; return 1; }
  # The same design cut into many small kernels: values crossing kernels through state words,
  # registers that can no longer be written in place (pending words and the commit copy). A small
  # design is otherwise always ONE kernel, so only the large benchmarks would ever run these paths.
  local lg2lnast=()
  [ "$kind" = sv ] && lg2lnast=(--set pass.lg2lnast.passes=balance)
  LHD_SIM_LNAST_CHUNK=1 "$LHD_ABS" pass lnast-llvm --top "$top" "lg:$d/lg" "${lg2lnast[@]}" --emit-dir "sim:$d/kc" -q >"$d/gc.log" 2>&1 \
    || { cat "$d/gc.log" >&2; fail "$design: lnast-llvm (small kernels) failed"; return 1; }
  "$RUN" "$d/kc" "$CYCLES" >"$d/cut.trace" || { fail "$design: small-kernel run failed"; return 1; }
  cmp -s "$d/cut.trace" "$d/ref.trace" || { diff "$d/cut.trace" "$d/ref.trace" | head -10 >&2; fail "$design: small-kernel traces differ"; return 1; }
  # Activity gating (a kernel runs only when something it reads changed), which a small design does
  # not get on its own: whole, and cut into small kernels (where nearly every value crosses kernels
  # and every register, memory and latch is read by some other kernel).
  local chunk
  for chunk in 0 1; do
    LHD_SIM_LNAST_GATE=1 LHD_SIM_LNAST_CHUNK=$chunk "$LHD_ABS" pass lnast-llvm --top "$top" "lg:$d/lg" "${lg2lnast[@]}" \
      --emit-dir "sim:$d/kg$chunk" -q >"$d/gg$chunk.log" 2>&1 \
      || { cat "$d/gg$chunk.log" >&2; fail "$design: lnast-llvm (gated, chunk $chunk) failed"; return 1; }
    "$RUN" "$d/kg$chunk" "$CYCLES" >"$d/gated$chunk.trace" || { fail "$design: gated run (chunk $chunk) failed"; return 1; }
    cmp -s "$d/gated$chunk.trace" "$d/ref.trace" \
      || { diff "$d/gated$chunk.trace" "$d/ref.trace" | head -10 >&2; fail "$design: gated traces differ (chunk $chunk)"; return 1; }
  done
  if [ "$kind" = prp ]; then
    # A short loop is unrolled in the native code; the rolled form (a loop past the unroll budget) must
    # give the same trace.
    LHD_SIM_LNAST_NO_UNROLL=1 "$LHD_ABS" pass lnast-llvm --top "$top" "lg:$d/lg" --emit-dir "sim:$d/kr" -q >"$d/gr.log" 2>&1 \
      || { cat "$d/gr.log" >&2; fail "$design: lnast-llvm (rolled) failed"; return 1; }
    "$RUN" "$d/kr" "$CYCLES" >"$d/rolled.trace" || { fail "$design: rolled kernel run failed"; return 1; }
    cmp -s "$d/rolled.trace" "$d/ref.trace" || { diff "$d/rolled.trace" "$d/ref.trace" | head -10 >&2; fail "$design: rolled traces differ"; return 1; }
  fi
  echo "ok $design"
}

# SystemVerilog designs: registers, resets, memories (ports, arrays, byte enables), hierarchy, and the
# clock-phase cases (a negedge register against a posedge one; a hi/lo latch pair that is a ring).
SV="comb_reg reset_enable reduce_shift struct_port hier async_reset m3_memory m3_memory_array mem_reset mem_reload mem_update
    mem_collide nary_ops mem_bytes setmask negedge_reg latch_pair"
# Pyrope designs. Compact loops: a `for` with its body inline -- carries, finals, nested loops, a
# memory read in place (also while this cycle's write to it is visible), strided domains, calls in the
# body. clock_gate: a register on `Clock(clock_pin=, enable=)` next to one on the free clock.
PRP="loop_mem loop_overlay loop_nested loop_step loop_call clock_gate"

pids=()
names=()
failed=0
reap() {  # wait for the oldest running design
  wait "${pids[0]}" || { failed=1; echo "FAILED ${names[0]}" >&2; }
  pids=("${pids[@]:1}")
  names=("${names[@]:1}")
}
for entry in $(for s in $SV; do echo "$s:sv"; done) $(for p in $PRP; do echo "$p:prp"; done); do
  design=${entry%%:*}
  kind=${entry##*:}
  one_design "$design" "$kind" &
  pids+=($!)
  names+=("$design")
  if [ "${#pids[@]}" -ge "$JOBS" ]; then
    reap
  fi
done
while [ "${#pids[@]}" -gt 0 ]; do
  reap
done
[ "$failed" -eq 0 ] || { echo "FAIL: at least one design differs (see above)" >&2; exit 1; }
