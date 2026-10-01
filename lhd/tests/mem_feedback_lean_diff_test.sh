#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# BEHAVIOURAL evidence that a NON-FORWARDING or REGISTERED memory read feeding
# its own memory's write port is modelled CORRECTLY, not merely accepted.
#
# The qualifier is load-bearing. A read that IS forwarded from that write port
# genuinely depends on this cycle's write data, and such a cycle must still be
# refused -- which is what the forwarding negative control below demonstrates
# from the value side, and pass/lean/cert_dag_order_test.cpp from the order side.
#
# lhd/tests/mem_read_write_feedback_test.sh checks the certificate's STRUCTURE
# -- the read is based on the committed array image, the sync read output is a
# register source.  That is the right shape, but a shape is not a value: a model
# that forwarded the write into the read, or that let a consumer of a
# synchronous port see the ungated read, would have the wrong numbers while
# still emitting and still passing checkDesign.  So this runs the source RTL
# under iverilog against the Lean certificate, period by period, and pairs the
# match with two mutants that must MISMATCH ON A VALUE.
#
# P=1, not 2. pass.single_edge SKIPS both fixtures -- one posedge clock, no
# latch, no negedge state -- so one source period is one Lean step.  The RTL is
# therefore sampled BEFORE the posedge (`--sample before-first-edge`), because
# `directStepRaw` reports PRE-transition outputs: sampling after would compare
# Lean's f(state_k, in_k) against the RTL's f(state_k+1, in_k) and report an
# off-by-one as a value mismatch.
#
# NOT A BAZEL TARGET. It needs the Lean toolchain and the multi-GB .lake tree,
# which cannot be runfiles. Run it directly:
#     bash lhd/tests/mem_feedback_lean_diff_test.sh
#
# NO IVERILOG IS A FAILURE. This file IS the behavioural gate; a run without a
# simulator produces no behavioural evidence, and reporting that as a pass is
# how a gate quietly stops gating. ALLOW_NO_IVERILOG=1 turns it into an
# explicit, loudly-labelled non-result that still exits non-zero.
set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; }
LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DIFF="$ROOT/scripts/mem_lean_diffsim.py"
SWEEP="$ROOT/pass/lean/scripts/direct_sweep.py"
[ -r "$DIFF" ]  || { echo "FAIL: missing $DIFF"; exit 1; }
[ -r "$SWEEP" ] || { echo "FAIL: missing $SWEEP"; exit 1; }

T="$ROOT/generated/tests/mem_feedback_lean_diff"
rm -rf "$T"; mkdir -p "$T"
export TMPDIR="$T/runtime_tmp"; mkdir -p "$TMPDIR"
fails=0
skipped=0

# The DUT state the Lean model's zeroState describes, written out explicitly on
# the RTL side.  Verilog regs and an uninferred memory start as X, and comparing
# X against a model is comparing against noise.
cat > "$T/init_async.v" <<'EOF'
    begin : zs
      integer k;
      for (k = 0; k < 16; k = k + 1) dut.mem[k] = 8'h00;
    end
EOF
cat > "$T/init_sync.v" <<'EOF'
    begin : zs
      integer k;
      for (k = 0; k < 16; k = k + 1) dut.mem[k] = 8'h00;
      dut.rq = 8'h00;
    end
EOF

# One scenario for both fixtures: they share a port list.
#   * writes at DIFFERENT addresses with changing data, so a stale or forwarded
#     read shows up as a wrong stored value;
#   * both arms of the `we ^ rd[0]` enable (the array starts even, so `we`
#     writes; once an odd value is stored, `!we` writes);
#   * SAME-address read/write collisions, which is the only place a forwarding
#     mistake is observable at all;
#   * reads of an address written in the PREVIOUS period, which is where a
#     sync port's register delay shows up.
python3 - "$T/scenario.json" <<'PYS'
import json, sys
p = []
# 1-5: write odd/even data at distinct addresses while the read sits elsewhere
for i, (d, wa, ra) in enumerate([(0xA1, 1, 0), (0xB2, 2, 1), (0xC3, 3, 2),
                                 (0xD4, 4, 3), (0x5F, 5, 4)]):
    p.append({"we": 1, "waddr": wa, "wdata": d, "raddr": ra})
# 6-8: read back what was written, at the addresses just used
for ra in (1, 2, 3):
    p.append({"we": 0, "waddr": 9, "wdata": 0x11, "raddr": ra})
# 9-11: SAME address for read and write -- the collision window
p.append({"we": 1, "waddr": 6, "wdata": 0x63, "raddr": 6})
p.append({"we": 0, "waddr": 6, "wdata": 0x64, "raddr": 6})
p.append({"we": 1, "waddr": 6, "wdata": 0x65, "raddr": 6})
# 12-14: the enable's other arm -- reading an ODD value flips `we ^ rd[0]`
p.append({"we": 1, "waddr": 7, "wdata": 0x77, "raddr": 1})
p.append({"we": 0, "waddr": 8, "wdata": 0x88, "raddr": 1})
p.append({"we": 1, "waddr": 8, "wdata": 0x89, "raddr": 7})
# 15-16: next-period observation of the last two writes
p.append({"we": 0, "waddr": 0, "wdata": 0, "raddr": 8})
p.append({"we": 0, "waddr": 0, "wdata": 0, "raddr": 6})
json.dump(p, open(sys.argv[1], "w"))
PYS

# The trace this scenario MUST produce, worked out by hand from the RTL rather
# than recorded from a run -- a recorded expectation cannot show that the
# scenario is non-vacuous, because a dead design records a clean trace of zeros.
# Columns are `q wcommit`, one row per source period, and the harness checks the
# RTL against them BEFORE comparing anything to Lean. If the two disagree the
# derivation below is what is wrong, and that is the right thing to find out.
#
# mem starts all-zero; q is sampled BEFORE the posedge (see the header).
#   async:  rd = mem[raddr]; wcommit = we ^ rd[0]; mem[waddr] <= wdata ^ rd
#   sync:   q = rq (latched at the end of the previous period)
cat > "$T/golden_mem_async_read_feedback.txt" <<'EOF'
0 1
161 0
0 1
195 0
0 1
161 1
0 0
195 1
0 1
99 1
7 0
161 0
161 1
0 1
137 1
7 1
EOF
cat > "$T/golden_mem_sync_read_feedback.txt" <<'EOF'
0 1
0 1
161 0
178 1
0 1
102 0
161 1
178 0
0 1
0 0
99 0
99 0
161 1
161 0
0 0
41 1
EOF

emit() {  # <top> -> $T/<top>/{cert.lean,io.json}
  local top="$1" d="$T/$1"
  mkdir -p "$d"
  ( cd "$d" && "$LHD" compile verilog "$HERE/$top.v" --top "$top" --reader yosys-slang \
      --workdir w --emit-dir lg:lg ) > "$d/compile.log" 2>&1 \
    || { echo "FAIL: $top did not import"; tail -3 "$d/compile.log"; return 1; }
  "$LHD" pass single_edge --top "$top" "lg:$d/lg" --emit-dir "lg:$d/lgn" \
    --set multi_clock=true --workdir "$d/se" > "$d/se.log" 2>&1 \
    || { echo "FAIL: $top: single_edge refused it"; return 1; }
  "$LHD" compile "lg:$d/lgn" --top "$top" --workdir "$d/lw" --emit-dir "lean:$d/lean" \
    --set formal.lean.mode=verified_compiler --set formal.lean.strict=true \
    > "$d/lean.log" 2>&1 \
    || { echo "FAIL: $top: no certificate"; grep -oP '"message":"\K[^"]{0,180}' "$d/lean.log" | head -1; return 1; }
  return 0
}

# Sets DIFF_OUT and DIFF_RC.  The exit status is captured DIRECTLY: matching a
# marker in mixed output would also accept a stale line from an earlier run or a
# marker printed before a later failure.
run_diff() {  # <top> <cert> <sidecar> <outdir> <init>
  DIFF_OUT="$(python3 "$DIFF" --cert "$2" --sidecar "$3" --rtl "$HERE/$1.v" --top "$1" \
    --out "$4" --scenario "$T/scenario.json" --p 1 --sample before-first-edge \
    --rtl-init "$5" --lean-dir "$ROOT/formal/lean" 2>&1)"
  DIFF_RC=$?
}

# ---------------------------------------------------------------------------
# checkDesign + the direct simulator, over BOTH fixtures.
# A certificate that is not ACCEPTED has no behaviour to compare.
# ---------------------------------------------------------------------------
for top in mem_async_read_feedback mem_sync_read_feedback; do
  emit "$top" || { fails=$((fails+1)); continue; }
  echo "ok: $top emits a certificate"
done
mkdir -p "$T/sweeproot"
for top in mem_async_read_feedback mem_sync_read_feedback; do
  [ -d "$T/$top/lean" ] && { mkdir -p "$T/sweeproot/$top"; cp -a "$T/$top/lean" "$T/sweeproot/$top/"; }
done
python3 "$SWEEP" --out "$T/sweep.tsv" --jobs 1 --cycles 6 --max-rec-depth 20000000 \
  "$T/sweeproot" > "$T/sweep.log" 2>&1
sweep_rc=$?
acc="$(awk -F'\t' 'NR>1 && $2=="ACCEPTED"' "$T/sweep.tsv" 2>/dev/null | wc -l)"
if [ "$sweep_rc" -ne 0 ] || [ "$acc" -ne 2 ]; then
  echo "FAIL: checkDesign/directStep accepted $acc of 2 (sweep rc=$sweep_rc); see $T/sweep.log"
  tail -5 "$T/sweep.log" | sed 's/^/      /'
  fails=$((fails+1))
else
  echo "ok: checkDesign ACCEPTED both certificates and ran 6 cycles"
fi

# ---------------------------------------------------------------------------
# POSITIVE: RTL vs Lean, period by period.
# ---------------------------------------------------------------------------
for top in mem_async_read_feedback mem_sync_read_feedback; do
  [ -d "$T/$top/lean" ] || continue
  init="$T/init_async.v"; [ "$top" = mem_sync_read_feedback ] && init="$T/init_sync.v"
  run_diff "$top" "$T/$top/lean/${top}_Lgraph.lean" "$T/$top/lean/${top}_io.json" "$T/$top/pos" "$init"
  echo "$DIFF_OUT" | sed 's/^/    /'
  if echo "$DIFF_OUT" | grep -q "BEHAVIORAL_DIFF_SKIPPED"; then
    echo "BEHAVIORAL_DIFF_SKIPPED $top (iverilog unavailable -- NO behavioural evidence)"
    skipped=$((skipped+1))
    continue
  fi
  if [ "$DIFF_RC" -ne 0 ] || ! echo "$DIFF_OUT" | grep -q "BEHAVIORAL_DIFF_PASS"; then
    echo "FAIL: $top does not match the RTL (rc=$DIFF_RC)"; fails=$((fails+1)); continue
  fi

  # ---- IS THE TRACE WORTH ANYTHING? -------------------------------------
  # A match proves the two models agree; it does not prove they did anything.
  # The RTL trace is checked against a HAND-DERIVED golden trace, and then
  # against the three properties that make this scenario non-vacuous.
  RTL="$T/$top/pos/rtl.out"
  if ! diff -q <(grep -E '^[0-9 ]+$' "$RTL") "$T/golden_$top.txt" > /dev/null; then
    echo "FAIL: $top's RTL trace is not the hand-derived golden trace"
    diff <(grep -E '^[0-9 ]+$' "$RTL") "$T/golden_$top.txt" | head -8 | sed 's/^/      /'
    fails=$((fails+1)); continue
  fi
  echo "ok: $top's RTL trace equals the hand-derived golden trace"
  RTLFILE="$RTL" python3 - <<'PYA' || fails=$((fails+1))
import os, sys
rows = [r.split() for r in open(os.environ["RTLFILE"]) if r.strip() and r.strip().replace(" ", "").isdigit()]
q = [int(r[0]) for r in rows]
wc = [int(r[1]) for r in rows]
bad = []
if sum(wc) < 2:
    bad.append(f"only {sum(wc)} period(s) committed a write; the write path is dead")
nz = sorted({v for v in q if v != 0})
if not nz:
    bad.append("no memory word ever read back nonzero, so nothing was stored")
if len(nz) < 2:
    bad.append(f"only {len(nz)} distinct nonzero value(s) observed ({nz}); a single "
               f"value could come from one lucky write")
# periods 8..10 read and write ADDRESS 6 -- the collision window -- and periods
# 9, 10 read what period 8/9 wrote. Both must actually show up.
if not (wc[8] == 1):
    bad.append("the same-address collision period (8) did not commit a write")
if q[9] == 0 and q[10] == 0:
    bad.append("neither period 9 nor 10 observed what the collision wrote, so the "
               "previous-cycle read case is not reached")
for b in bad:
    print(f"FAIL: {b}")
sys.exit(1 if bad else 0)
PYA
  [ $? -eq 0 ] && echo "ok: $top's trace commits writes, stores distinct values, and reaches the collision"
done

# ---------------------------------------------------------------------------
# NEGATIVE CONTROLS.  Each must mismatch ON A VALUE; an emit refusal would not
# show that the comparison can see the defect.  Both are applied to the
# CERTIFICATE (test side), never through a production switch.
# ---------------------------------------------------------------------------
if [ "$skipped" -eq 0 ] && [ -d "$T/mem_async_read_feedback/lean" ]; then
  # (a) pretend the read IS forwarded from the write: re-base the Op_MemRead on
  #     the write-chain node instead of the committed array image.
  mkdir -p "$T/neg_fwd"
  python3 - "$T/mem_async_read_feedback/lean/mem_async_read_feedback_Lgraph.lean" \
            "$T/neg_fwd/cert.lean" <<'PYM'
import re, sys
src = open(sys.argv[1]).read()
entries = re.search(r"sources\s*:=\s*#\[(.*?)\n\s*\]", src, re.S).group(1)
nsrc = len([l for l in entries.strip().splitlines() if l.strip()])
nodes = re.findall(r"\{ op := (LGraphOp\.\S+(?: \d+)?), width := \d+, deps := #\[([0-9, ]*)\], origin := \d+ \}", src)
wr = [nsrc + i for i, (op, d) in enumerate(nodes) if op.startswith("LGraphOp.Op_MemWrite")]
rd = [(i, d) for i, (op, d) in enumerate(nodes) if op == "LGraphOp.Op_MemRead"]
if len(wr) != 1 or len(rd) != 1:
    print(f"MUTATION-UNAVAILABLE writes={len(wr)} reads={len(rd)}"); sys.exit(2)
i, deps = rd[0]
a = [x.strip() for x in deps.split(",")]
old = f"deps := #[{deps}]"
new = f"deps := #[{wr[0]}, {a[1]}, {a[2]}]"
if src.count(old) != 1:
    print("MUTATION-UNAVAILABLE the read's dep list is not unique"); sys.exit(2)
open(sys.argv[2], "w").write(src.replace(old, new, 1))
PYM
  if [ $? -ne 0 ]; then
    echo "FAIL: could not build the forwarding mutant, so that control is untested"
    fails=$((fails+1))
  else
    cp "$T/mem_async_read_feedback/lean/mem_async_read_feedback_io.json" "$T/neg_fwd/"
    run_diff mem_async_read_feedback "$T/neg_fwd/cert.lean" \
         "$T/neg_fwd/mem_async_read_feedback_io.json" "$T/neg_fwd/run" "$T/init_async.v"
    n="$DIFF_OUT"
    if [ "$DIFF_RC" -ne 0 ] && echo "$n" | grep -q "BEHAVIORAL_DIFF_FAIL.*period"; then
      echo "ok: NEGATIVE a FORWARDED read mismatches on a value"
      echo "$n" | grep -m1 "period " | sed 's/^/      /'
    else
      echo "FAIL: forwarding the write into the read still matched, so the"
      echo "      scenario never observes a read/write collision"
      echo "$n" | head -3 | sed 's/^/      /'
      fails=$((fails+1))
    fi
  fi

  # (b) make a consumer of the SYNCHRONOUS port see the ungated read instead of
  #     the read-data register: the value is then a cycle early.
  mkdir -p "$T/neg_sync"
  python3 - "$T/mem_sync_read_feedback/lean/mem_sync_read_feedback_Lgraph.lean" \
            "$T/neg_sync/cert.lean" <<'PYM'
import re, sys
src = open(sys.argv[1]).read()
entries = re.search(r"sources\s*:=\s*#\[(.*?)\n\s*\]", src, re.S).group(1)
elist = [l.strip().lstrip(", ").strip() for l in entries.strip().splitlines()]
nsrc = len(elist)
flop = [i for i, e in enumerate(elist) if e.startswith("SourceDesc.flopQ")]
nodes = re.findall(r"\{ op := (LGraphOp\.\S+(?: \d+)?), width := (\d+), deps := #\[([0-9, ]*)\], origin := (\d+) \}", src)
raw = [nsrc + i for i, (op, w, d, o) in enumerate(nodes) if op == "LGraphOp.Op_MemRead"]
mux = [nsrc + i for i, (op, w, d, o) in enumerate(nodes) if op == "LGraphOp.Op_MuxBool"]
if len(flop) != 1 or len(raw) != 1 or len(mux) != 1:
    print(f"MUTATION-UNAVAILABLE flopQ={len(flop)} raw={len(raw)} mux={len(mux)}"); sys.exit(2)
reg, r = flop[0], raw[0]
out = []
changed = 0
for op, w, d, o in nodes:
    ds = [x.strip() for x in d.split(",") if x.strip()]
    slot = nsrc + len(out)
    if slot != mux[0] and str(reg) in ds:
        ds = [str(r) if x == str(reg) else x for x in ds]
        changed += 1
    out.append((op, w, ds, o))
if changed == 0:
    print("MUTATION-UNAVAILABLE nothing outside the register's own mux reads it"); sys.exit(2)
new = src
for (op, w, ds, o), (op0, w0, d0, o0) in zip(out, nodes):
    if ds != [x.strip() for x in d0.split(",") if x.strip()]:
        a = "{ op := %s, width := %s, deps := #[%s], origin := %s }" % (op0, w0, d0, o0)
        b = "{ op := %s, width := %s, deps := #[%s], origin := %s }" % (op0, w0, ", ".join(ds), o0)
        new = new.replace(a, b, 1)
open(sys.argv[2], "w").write(new)
PYM
  if [ $? -ne 0 ]; then
    echo "FAIL: could not build the sync-consumer mutant, so that control is untested"
    fails=$((fails+1))
  else
    cp "$T/mem_sync_read_feedback/lean/mem_sync_read_feedback_io.json" "$T/neg_sync/"
    run_diff mem_sync_read_feedback "$T/neg_sync/cert.lean" \
          "$T/neg_sync/mem_sync_read_feedback_io.json" "$T/neg_sync/run" "$T/init_sync.v"
    n2="$DIFF_OUT"
    if [ "$DIFF_RC" -ne 0 ] && echo "$n2" | grep -q "BEHAVIORAL_DIFF_FAIL.*period"; then
      echo "ok: NEGATIVE a sync consumer reading the UNGATED read mismatches on a value"
      echo "$n2" | grep -m1 "period " | sed 's/^/      /'
    else
      echo "FAIL: a sync consumer reading the ungated read still matched, so the"
      echo "      register delay is not observable in this scenario"
      echo "$n2" | head -3 | sed 's/^/      /'
      fails=$((fails+1))
    fi
  fi
fi

[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s) failed"; exit 1; }
if [ "$skipped" -ne 0 ]; then
  # NOT a pass. This file IS the behavioural gate, so a run with no simulator
  # produced no behavioural evidence, and saying "pass" would let the gate
  # evaporate the moment a machine lost iverilog.
  echo "NO-BEHAVIORAL-EVIDENCE: $skipped fixture(s) had no iverilog."
  if [ "${ALLOW_NO_IVERILOG:-0}" = "1" ]; then
    echo "  ALLOW_NO_IVERILOG=1: reported as an explicit NON-RESULT, still exit 2."
    echo "  It may NOT be cited as behavioural evidence."
  else
    echo "  Install iverilog, or set ALLOW_NO_IVERILOG=1 to acknowledge the gap."
  fi
  exit 2
fi
echo "PASS: mem_feedback_lean_diff_test (checkDesign + BEHAVIORAL_DIFF + 2 negative controls)"
