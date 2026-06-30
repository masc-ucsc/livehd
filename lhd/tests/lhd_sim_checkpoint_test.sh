#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` CHECKPOINT creation (sim_checkpoint_debug_plan, Stage B):
#   * the generated driver carries a periodic fork-checkpoint cadence in the tick
#     loop (dump_state -> regs.json + <mem>.hex, the testbench frame -> tb.json,
#     meta.json with cycle/design_hash/seed); cgen emits dump_state/load_state/
#     design_hash per module;
#   * `lhd sim --set sim.checkpoint_every=N --set sim.checkpoint_max=M` writes
#     `<workdir>/ckpt/<test>/ckp<cycle>/` dirs, pruned to M and evenly spaced;
#   * `--set sim.checkpoint=false` disables it (no dirs).
# Structural checks run hermetically; the run checks need the sibling ../hlop +
# ../iassert headers (a dev / repo-root run).

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_ckpt_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# A design with a flop, a stateful sub-instance, and a registered memory, plus a
# testbench with an accumulating local (`total`) -> the checkpoint must capture
# all four kinds of state.
cat > "$W/ck.prp" <<'EOF'
/*
:name: ck
:type: simulation
*/
mod sub(en:bool) -> (o:u8@[0]) { reg c:u8 = 0; o = c; if en { wrap c += 1 } }
mod top(en:bool, din:u8, inp:u80) -> (sout:u8@[0], fout:u8@[0], mout:u10@[0]) {
  reg acc:u8 = 0
  if en { wrap acc += din }
  fout = acc
  sout = sub(en=en)
  reg arr:[8]u10 = nil
  arr  = inp
  mout = arr[0]
}
test top.run {
  mut acc   = top
  mut total = 0
  tick 12 {
    acc.en = true; acc.din = 1; acc.inp = clock
    step
    total = total + 1
  }
  assert(total == 12)
}
EOF

# ---- structural: cgen emits the state methods; the driver has the cadence -----
"$LHD" sim "$W/ck.prp" --setup-only --workdir "$W/s" -q >/dev/null 2>&1 || fail "setup-only failed"
TOPCPP="$W/s/sim/ck.top.cpp"
DRV="$W/s/sim/drv.cpp"
[ -f "$TOPCPP" ] && [ -f "$DRV" ] || fail "generated sources missing"

grep -q '::dump_state'   "$TOPCPP" || fail "cgen did not emit dump_state"
grep -q '::load_state'   "$TOPCPP" || fail "cgen did not emit load_state"
grep -q '::design_hash'  "$TOPCPP" || fail "cgen did not emit design_hash"
grep -q 'write_mem_hex'  "$TOPCPP" || fail "dump_state does not write the memory hex"
grep -q 'dump_state(_p + "' "$TOPCPP" || fail "dump_state does not recurse into the sub-instance"
grep -q '#include "checkpoint.hpp"' "$TOPCPP" || fail "module source does not include checkpoint.hpp"

grep -q 'fork_checkpoint'       "$DRV" || fail "driver lacks the fork-checkpoint cadence"
grep -q 'prune_checkpoints'     "$DRV" || fail "driver lacks checkpoint pruning"
grep -q '"/tb.json"'            "$DRV" || fail "driver does not write the testbench frame"
grep -q '_tb\["total"\]'        "$DRV" || fail "driver does not checkpoint the tb local 'total'"
grep -q '"--ckpt-dir"'          "$DRV" || fail "driver does not accept --ckpt-dir"
grep -q '"--no-checkpoint"'     "$DRV" || fail "driver does not accept --no-checkpoint"

# ---- error: an unknown sim.* flag is rejected with the namespace hint ----------
EO="$("$LHD" sim "$W/ck.prp" --set sim.checkpoint_bogus=1 --setup-only --workdir "$W/e" -q 2>&1)" \
  && fail "unknown sim flag was not rejected"
echo "$EO" | grep -q "unknown sim flag 'sim.checkpoint_bogus'" || fail "wrong message for unknown sim flag: $EO"

# ---- opportunistic real build + run (needs the sibling runtime headers) -------
HLOP_INC=""
IASSERT_INC=""
for d in ../hlop/hlop ../hlop; do [ -f "$d/slop.hpp" ] && HLOP_INC="$d" && break; done
for d in ../iassert/src ../iassert; do [ -f "$d/iassert.hpp" ] && IASSERT_INC="$d" && break; done
if [ -z "$HLOP_INC" ] || [ -z "$IASSERT_INC" ]; then
  echo "SKIP run checks: sibling hlop/iassert headers not found (structural checks passed)"
  echo "PASS: lhd sim checkpoint creation (structural)"
  exit 0
fi

# checkpoint every 2 cycles, keep at most 3 -> evenly-spaced subset of {2,4,..,10}
"$LHD" sim "$W/ck.prp" --set sim.checkpoint_every=2 --set sim.checkpoint_max=3 --workdir "$W/run" -q >/dev/null 2>&1 \
  || fail "checkpoint run failed"
CKDIR="$W/run/ckpt/top_run"
[ -d "$CKDIR" ] || fail "no checkpoint dir created under the workdir"

N=$(ls -d "$CKDIR"/ckp* 2>/dev/null | wc -l | tr -d ' ')
[ "$N" = "3" ] || fail "expected 3 checkpoints after prune, got $N: $(ls "$CKDIR")"

# every checkpoint has all four artifacts
for d in "$CKDIR"/ckp*; do
  [ -s "$d/regs.json" ] || fail "$d missing regs.json"
  [ -s "$d/tb.json" ]   || fail "$d missing tb.json"
  [ -s "$d/meta.json" ] || fail "$d missing meta.json"
  ls "$d"/*.hex >/dev/null 2>&1 || fail "$d missing a memory .hex"
done

# the latest checkpoint carries hierarchical reg names + the accumulating tb local
LATEST="$(ls -d "$CKDIR"/ckp* | sort -t p -k3 -n | tail -1)"
grep -q '"acc.acc"'           "$LATEST/regs.json" || fail "regs.json lacks the top flop acc.acc: $(cat "$LATEST/regs.json")"
grep -q '"acc.u_'             "$LATEST/regs.json" || fail "regs.json lacks the sub-instance reg: $(cat "$LATEST/regs.json")"
grep -q '"total"'             "$LATEST/tb.json"   || fail "tb.json lacks the accumulating local: $(cat "$LATEST/tb.json")"
grep -q '"design_hash"'       "$LATEST/meta.json" || fail "meta.json lacks design_hash"
grep -q '"seed"'              "$LATEST/meta.json" || fail "meta.json lacks seed"

# --set sim.checkpoint=false writes nothing
"$LHD" sim "$W/ck.prp" --set sim.checkpoint=false --set sim.checkpoint_every=2 --workdir "$W/off" -q >/dev/null 2>&1 \
  || fail "checkpoint-off run failed"
[ -d "$W/off/ckpt" ] && fail "ckpt dir created even with sim.checkpoint=false"

echo "PASS: lhd sim checkpoint creation (host-compile: dirs, prune, tb-frame, meta, disable)"
