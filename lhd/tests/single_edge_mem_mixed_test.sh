#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.single_edge must gate memory ports PER PORT.
#
# Every memory site in that pass used to ask `is_read && me.type != 1`: a
# per-port question answered with the cell-global `type`. That holds only while
# all read ports agree. On a memory whose reads mix clocked and unclocked, the
# global answer is wrong for some port -- and specifically it would treat the
# clocked read-data registers as combinational and leave them UNSLOTTED,
# committing on every sub-step, which is the lowering this pass exists never to
# emit.
#
# WHY THE FIXTURE CARRIES A NEGEDGE FLOP. On the plain mixed-memory fixture the
# pass reports "skipped: no latch, no negedge state, one clock net" and never
# runs its phase lowering at all, so it exercises no commit gating whatsoever.
# The negedge element forces P=2, which is the only configuration in which the
# claim below is observable.
set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; }
LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RTL="$HERE/mem_mixed_rdclk_negedge.v"
[ -r "$RTL" ] || { echo "FAIL: missing fixture $RTL"; exit 1; }

T="${TEST_TMPDIR:-$(cd "$HERE/../.." && pwd)/generated/tests}/single_edge_mem_mixed"
rm -rf "$T"; mkdir -p "$T"; cd "$T" || exit 1
fails=0

LIVEHD_MEM_TIMING_DEBUG=1 "$LHD" compile verilog "$RTL" --top mem_mixed_rdclk_negedge \
  --reader yosys-slang --workdir "$T/w" --emit-dir "lg:$T/lg" > "$T/compile.log" 2>&1 \
  || { echo "FAIL: the fixture did not import"; tail -3 "$T/compile.log"; exit 1; }
grep -q "port0=async port1=async port2=async port3=sync port4=sync" "$T/compile.log" \
  && echo "ok: imported with 3 async + 2 sync read ports" \
  || { echo "FAIL: the fixture no longer imports as 3 async + 2 sync"
       grep -oP 'memory read timing: \K.*' "$T/compile.log" | head -1 | sed 's/^/      /'; fails=$((fails+1)); }

LIVEHD_SE_MEM_GATE_DEBUG=1 "$LHD" pass single_edge --top mem_mixed_rdclk_negedge "lg:$T/lg" \
  --emit-dir "lg:$T/lg_norm" --set multi_clock=true --workdir "$T/se" > "$T/se.log" 2>&1
if [ $? -ne 0 ]; then
  echo "FAIL: pass.single_edge refused the mixed-timing memory"
  grep -oP '"message":"\K[^"]{0,140}' "$T/se.log" | head -1 | sed 's/^/      /'
  exit 1
fi

# The pass must actually RUN its lowering. "skipped" here would make every
# assertion below vacuous.
if grep -q "skipped" "$T/se.log"; then
  echo "FAIL: pass.single_edge SKIPPED, so no commit gating was exercised"
  fails=$((fails+1))
elif grep -q "P=2 slots" "$T/se.log"; then
  echo "ok: edge normalization ran with P=2 slots"
else
  echo "FAIL: expected P=2 slots"; grep -oP '"message":"\K[^"]{0,120}' "$T/se.log" | head -1 | sed 's/^/      /'
  fails=$((fails+1))
fi

# ---- the claim ---------------------------------------------------------
# write + SYNC reads are slot-gated; ASYNC reads stay live on both microsteps.
GATE="$(grep -oP 'pass\.single_edge: memory .*' "$T/se.log" | head -1)"
if [ -z "$GATE" ]; then
  echo "FAIL: no gate readback (LIVEHD_SE_MEM_GATE_DEBUG produced nothing)"; fails=$((fails+1))
else
  echo "    $GATE"
  for want in "port0=write:gated" "port1=async:live" "port2=async:live" \
              "port3=async:live" "port4=sync:gated" "port5=sync:gated"; do
    echo "$GATE" | grep -q -- "$want" \
      || { echo "FAIL: expected $want"; fails=$((fails+1)); }
  done
  # Stated as its own check so the failure reads as the SEMANTIC error it is.
  # `=`-anchored: "async:live" CONTAINS "sync:live", so an unanchored match
  # here reported a sync port as ungated on a perfectly correct run.
  echo "$GATE" | grep -q "=async:gated" \
    && { echo "FAIL: an ASYNCHRONOUS read port was slot-gated -- a combinational output would"
         echo "      become visible on only one microstep"; fails=$((fails+1)); }
  echo "$GATE" | grep -q "=sync:live" \
    && { echo "FAIL: a SYNCHRONOUS read port was left ungated -- its read-data register would"
         echo "      commit on every sub-step"; fails=$((fails+1)); }
  [ "$fails" -eq 0 ] && echo "ok: only the write and sync-read ports are gated; async reads stay live"
fi

# The normalized graph must still be emittable, or "gated correctly" would be
# an assertion about a graph nothing can use.
"$LHD" compile "lg:$T/lg_norm" --top mem_mixed_rdclk_negedge --workdir "$T/lw" \
  --emit-dir "lean:$T/lean" --set formal.lean.mode=verified_compiler \
  --set formal.lean.strict=true > "$T/lean.log" 2>&1 \
  && echo "ok: the normalized graph still emits a certificate" \
  || { echo "FAIL: the normalized mixed memory no longer emits"
       grep -oP '"message":"\K[^"]{0,120}' "$T/lean.log" | head -1 | sed 's/^/      /'; fails=$((fails+1)); }

[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s) failed"; exit 1; }
echo "PASS: single_edge_mem_mixed_test"
