#!/usr/bin/env bash
set -euo pipefail
# xpatch: B's state computed from A's run, with no cycle of B simulated, and proved right in Lean.
#
# Fixtures (gen.py): A, an accumulator with a power-on value, a free-running counter and a
# 12-word RAM; B, the same with a register on the accumulator's input (it holds each increment
# for one cycle); the product of A and B with a monitor and a relation R as its output `ok`; the
# same product with a wrong R; a 40-cycle testbench. Checks:
#   proof    sym_verify proves `ok` from power-on (k = 1: the base fact and the square); the
#            emitted theorem file embeds the product's certificate and is exactly sym_verify's
#            template; Lean compiles it, and refuses three corrupted copies of it
#   jump     only A runs (cert_trace from power-on, its state at every cycle); transport.py writes
#            B's state at cycles 10, 20, 30 and 40; lhd_ckpt import-lhd (no `fill`) writes each
#            as a checkpoint of B, and lhd_ckpt check validates it; nothing in this step runs B
#   compare  B from power-on: every jumped state equals B's state (lhd_ckpt diff, counted); B
#            resumes from the jumped cycle-20 state to cycle 40 with the same state and outputs;
#            the product's run, split by prefix, equals A's run, B's run and the monitor
# Controls: the wrong R is not k-inductive, has a counterexample from power-on, and its jumped
# states differ; import-lhd refuses a state with one of B's flops left out; A's outputs differ
# from B's; Lean's certificate reader refuses a non-certificate; checks.py's comparisons and the
# grader below each catch deliberately corrupted inputs. A RESULT line follows each command in
# $OUT/run.log, and the checks at the end grade them, each naming the log line it read.
#
# Needs lean and cadical on PATH (formal/lean's toolchain, 4.36.0-rc1) and formal/lean/.olean-dev
# (formal/lean/.build-proj.sh); the Lean drivers run interpreted against it. Most of the time goes
# to Lean compiling the emitted theorem and its three corrupted copies, in parallel.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT="${OUT:-$ROOT/generated/xpatch}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
for t in lean cadical; do command -v "$t" > /dev/null || { echo "$t is not on PATH" >&2; exit 2; }; done
[[ -n "$OUT" && "$OUT" != / && "$OUT" != "$ROOT" ]] || { echo "refusing OUT=$OUT" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
L="$OUT/run.log"; : > "$L"
LAST="$OUT/.last"; CHK="$OUT/.checks"; : > "$CHK"
OUTR="${OUT#"$ROOT"/}"
SV="$ROOT/pass/lean/tools/drivers/sym_verify.lean"
CT="$ROOT/formal/lean/Futamura_Projection_Scripts/cert_trace.lean"
CK="$ROOT/pass/lean/tools/drivers/lhd_ckpt.lean"

rel()  { sed "s|$ROOT/||g"; }                        # paths inside the checkout, relative to it
say()  { printf '%s\n' "$*" | rel >> "$L"; }
res()  { local lab=$1; shift; say "RESULT $lab: $*"; }
step() { say ""; say "== $*  ($(date -u +%Y-%m-%dT%H:%M:%SZ))"; echo "$*"; }
first() { grep -m1 -E -- "$1" "$LAST" || tail -1 "$LAST"; }   # the first line matching, else the last
# run [-C dir] <cmd...>: logs "$ <cmd>", its output, "[exit N]"; RC = the exit code; output kept in $LAST
run() {
  local dir=""
  if [[ $1 == -C ]]; then dir=$2; shift 2; fi
  if [[ -n $dir ]]; then say "\$ (cd $dir && $*)"; else say "\$ $*"; fi
  RC=0
  if [[ -n $dir ]]; then (cd "$dir" && "$@") > "$LAST" 2>&1 || RC=$?; else "$@" > "$LAST" 2>&1 || RC=$?; fi
  rel < "$LAST" >> "$L"; say "[exit $RC]"
}
# drv <driver .lean> <args...>: a Lean driver, interpreted from formal/lean ("[formal/lean]" in the log)
drv() {
  local src=$1; shift
  say "\$ [formal/lean] lean --run $src $*"
  RC=0
  (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$src" "$@") > "$LAST" 2>&1 || RC=$?
  rel < "$LAST" >> "$L"; say "[exit $RC]"
}
nsteps() { grep -c '^STEP ' "$LAST" || true; }

say "xpatch run.sh, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
say "checkout HEAD: $(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo 'not a git checkout')"
say "lean: $(lean --version)"
say "paths are shown relative to the checkout; [formal/lean] = run from formal/lean with LEAN_PATH=.olean-dev"
run sha256sum "$SCRIPT_DIR/run.sh" "$SCRIPT_DIR/gen.py" "$SCRIPT_DIR/transport.py" "$SCRIPT_DIR/checks.py" "$SCRIPT_DIR/Transport.lean"

# ------------------------------------------------------------------------------------------
step "step 0: the comparisons' own selftest (each must catch its deliberately corrupted inputs)"
run python3 "$SCRIPT_DIR/checks.py" selftest
res "checks selftest" "exit $RC: $(first '^checks selftest:')"

# ------------------------------------------------------------------------------------------
step "step 1: the designs (gen.py) and their certificates (lhd compile)"
D="$OUT/designs"
run python3 "$SCRIPT_DIR/gen.py" "$D"
res "designs" "$(first '^wrote')"
run python3 - "$D/tb.stim" <<'EOF'
import sys
v = {}
for l in open(sys.argv[1]).read().split('\n')[1:]:
    t = l.split()
    if t:
        v.setdefault(int(t[1]), {})[int(t[3])] = int(t[5])
print(f"tb.stim: {len(v)} cycles; en high in {sum(c[2] for c in v.values())}; we high in "
      f"{sum(c[5] for c in v.values())}; words written {sorted({c[4] for c in v.values() if c[5]})}")
EOF
res "testbench" "$(first '^tb.stim:')"
for n in A B product product_wrong; do
  d="$OUT/$n"; mkdir -p "$d"; cp "$D/$n.prp" "$d/"
  run -C "$d" "$LHD" compile "$n.prp" --top top --emit-dir lg:top_lg --workdir cw; r1=$RC
  run -C "$d" "$LHD" compile lg:top_lg --top top --workdir lw --emit-dir lean:top_lean --set formal.lean.mode=verified_compiler
  if [[ $r1 == 0 && $RC == 0 && -s "$d/top_lean/top.dcert" ]]; then
    res "certificate $n" "written, $(wc -c < "$d/top_lean/top.dcert") bytes, sha256 $(sha256sum "$d/top_lean/top.dcert" | cut -c1-16)"
  else
    res "certificate $n" "MISSING (lhd exits $r1, $RC)"
  fi
done
for n in A B product; do
  run python3 - "$OUT/$n/top_lean/top_io.json" <<'EOF'
import json, sys
io = json.load(open(sys.argv[1]))
got = {p['ordinal']: p['name'] for p in io['inputs']}
want = {0: 'clock', 1: 'din', 2: 'en', 3: 'reset', 4: 'wa', 5: 'we'}
print('input ordinals', got, 'match' if got == want else 'MISMATCH')
print('flops', [f['name'] for f in io['flops']], 'memories', [m['inst'] for m in io['memories']],
      'outputs', [o['name'] for o in io['outputs']])
EOF
  res "inputs $n" "$(first '^input ordinals')"
done

# ------------------------------------------------------------------------------------------
step "step 2: the proof, once per pair of designs (sym_verify on the product; Lean checks the theorem)"
P="$OUT/proof"; mkdir -p "$P"
run lean "$SCRIPT_DIR/Transport.lean"
res "Transport.lean" "lean exit $RC; $(grep -c 'does not depend on any axioms' "$LAST" || true) of 2 theorems print 'does not depend on any axioms'"
drv "$SV" "$OUT/product/top_lean/top.dcert" ok k:1 init:reset emit:"$P/Xpatch.lean" work:"$P/w_product"
res "prove product k:1 init:reset" "exit $RC: $(grep -oE 'RESULT: .*' "$LAST" | tail -1 || true)"
# the emitted theorem file, read: the design it embeds, its statement, three corrupted copies
run python3 "$SCRIPT_DIR/checks.py" thm "$P/Xpatch.lean" "$OUT/product/top_lean/top.dcert" "$OUT/product/top_lean/top_io.json" \
  "$OUT/product_wrong/top_lean/top.dcert" "$P/corrupted"
res "theorem file" "exit $RC: $(first '^thm:' | sed 's/^thm: //')"
# Lean's certificate reader on the product's certificate (so the theorem's D is not `default`)
cat > "$P/certprobe.lean" <<'EOF'
import LeanSemanticPrimitives.Compiler.CertIOCodec
open Compiler
def main (args : List String) : IO UInt32 := do
  let b ← IO.FS.readBinFile (System.FilePath.mk (args.headD ""))
  match CertIO.readCertChecked b with
  | .ok d =>
    IO.println s!"readCertChecked: ok; {d.flops.size} flops, {d.memories.size} memories, {d.outputs.size} outputs, {d.nodes.size} nodes"
    return 0
  | .error e =>
    IO.println s!"readCertChecked: error {e}"
    return 1
EOF
printf 'not a certificate\n' > "$P/not_a_certificate.dcert"
drv "$P/certprobe.lean" "$OUT/product/top_lean/top.dcert"
res "certificate reader" "exit $RC: $(first '^readCertChecked:')"
drv "$P/certprobe.lean" "$P/not_a_certificate.dcert"
res "certificate reader on non-certificate" "exit $RC: $(first '^readCertChecked:')"
# Lean compiles the theorem and its three corrupted copies, in parallel; each copy must be refused
lean_thm() { # <file> <log>: the emitted files need a large stack and no heartbeat limit
  local t0 rc=0; t0=$(date +%s)
  (cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$PWD/.olean-dev" \
     timeout 3600 lean -DmaxHeartbeats=0 -DmaxRecDepth=1000000 "$1") > "$2" 2>&1 || rc=$?
  echo "[exit $rc, $(( $(date +%s) - t0 )) s wall]" >> "$2"
  return $rc
}
KINDS=(theorem badproof wrongdesign baddesign)
FILES=("$P/Xpatch.lean" "$P/corrupted/badproof.lean" "$P/corrupted/wrongdesign.lean" "$P/corrupted/baddesign.lean")
PIDS=()
for i in 0 1 2 3; do
  say "\$ [formal/lean] ulimit -s unlimited; lean -DmaxHeartbeats=0 -DmaxRecDepth=1000000 ${FILES[$i]}   (background; log $P/lean_${KINDS[$i]}.log)"
  lean_thm "${FILES[$i]}" "$P/lean_${KINDS[$i]}.log" & PIDS+=($!)
done
for i in 0 1 2 3; do
  k=${KINDS[$i]}; rc=0; wait "${PIDS[$i]}" || rc=$?
  say "--- $P/lean_$k.log"; rel < "$P/lean_$k.log" >> "$L"
  wall=$(grep -oE '[0-9]+ s wall' "$P/lean_$k.log" | tail -1 || true)
  if [[ $k == theorem ]]; then
    ax=$(tr -d '\n' < "$P/lean_$k.log" | grep -oE "'SymF2\.Xpatch\.invariant' depends on axioms: \[[^]]*\]" | sed 's/.*depends on axioms: //' || true)
    sorry=$(grep -c sorryAx "$P/lean_$k.log" || true)
    res "lean theorem" "exit $rc, $wall; axioms: ${ax:-none printed}$([[ $sorry != 0 ]] && echo "; sorryAx")"
  else
    res "lean corrupted $k" "exit $rc, $wall; first error: $(grep -m1 -A3 'error' "$P/lean_$k.log" | tr '\n' ' ' | cut -c1-300 || true)"
  fi
done
# the control: the same proof attempt on the wrong relation, and a counterexample from power-on
drv "$SV" "$OUT/product_wrong/top_lean/top.dcert" ok k:1 init:reset work:"$P/w_wrong_k1"
res "prove product_wrong k:1 init:reset" "exit $RC: $(grep -oE 'RESULT: .*' "$LAST" | tail -1 || true)"
drv "$SV" "$OUT/product_wrong/top_lean/top.dcert" ok k:25 base init:reset work:"$P/w_wrong_base"
res "prove product_wrong base k:25 init:reset" "exit $RC: $(grep -oE 'RESULT: .*' "$LAST" | tail -1 || true)"

# ------------------------------------------------------------------------------------------
step "step 3: the jump (only A runs; nothing in this step runs B or a product design)"
J0=$(wc -l < "$L")
AS="$OUT/A/states"; mkdir -p "$AS"
ats=(); for j in $(seq 0 40); do ats+=("ckpt-at:$j:$AS/$j.ckpt"); done
drv "$CT" "$OUT/A/top_lean/top.dcert" 40 "$D/tb.stim" "${ats[@]}"
cp "$LAST" "$OUT/A/poweron.trace"
res "run A" "exit $RC, $(ls "$AS" | wc -l) states (cycles 0..40), $(nsteps) STEP lines"
for R in product product_wrong; do
  JP="$OUT/jump_$R"
  run python3 "$SCRIPT_DIR/transport.py" $R "$AS" "$OUT/A/top_lean/top_io.json" "$D/tb.stim" 10,20,30,40 "$JP"
  res "jump $R" "exit $RC: $(first "^transport $R: points" | sed 's/^[^;]*; //')"
  res "jump $R pending" "$(first "^transport $R: an increment" | sed 's/^transport [^:]*: //')"
  for n in 10 20 30 40; do
    drv "$CK" import-lhd "$OUT/B/top_lean/top.dcert" "$JP/ckp$n" "$JP/$n.ckpt" inst:t
    imp="import exit $RC: $(first '^(OK|REFUSED)')"
    drv "$CK" check "$OUT/B/top_lean/top.dcert" "$JP/$n.ckpt"
    res "import $R $n" "$imp; check exit $RC: $(first '^(OK|REFUSED)')"
  done
done
# the import is strict (no `fill`): the cycle-40 state with B's flop inc left out must be refused
NEG="$OUT/jump_incomplete/ckp40"; mkdir -p "$NEG"; cp "$OUT/jump_product/ckp40/"* "$NEG/"
python3 -c "import json,sys; p=sys.argv[1]; d=json.load(open(p)); del d['t.inc']; json.dump(d, open(p,'w'))" "$NEG/regs.json"
drv "$CK" import-lhd "$OUT/B/top_lean/top.dcert" "$NEG" "$OUT/jump_incomplete/40.ckpt" inst:t
res "import without flop inc" "exit $RC: $(first '^(OK|REFUSED)')"
J1=$(wc -l < "$L")
cnt() { sed -n "$((J0 + 1)),${J1}p" "$L" | grep -E '^\$ ' | grep -cF -- "$1" || true; }
res "simulator commands in step 3" "cert_trace on A $(cnt "cert_trace.lean $OUTR/A/"), on B $(cnt "cert_trace.lean $OUTR/B/"), on a product $(cnt "cert_trace.lean $OUTR/product"); lhd sim $(cnt 'lhd sim'); sym_verify $(cnt 'sym_verify.lean')"

# ------------------------------------------------------------------------------------------
step "step 4: the comparison (only now B, and the product designs, run from power-on)"
G="$OUT/B/states"; mkdir -p "$G"
ats=(); for j in $(seq 0 40); do ats+=("ckpt-at:$j:$G/$j.ckpt"); done
drv "$CT" "$OUT/B/top_lean/top.dcert" 40 "$D/tb.stim" "${ats[@]}"
cp "$LAST" "$OUT/B/poweron.trace"
res "B from power-on" "exit $RC, $(ls "$G" | wc -l) states (cycles 0..40), $(nsteps) STEP lines"
for R in product product_wrong; do
  same=""; differ=""
  for n in 10 20 30 40; do
    drv "$CK" diff "$OUT/jump_$R/$n.ckpt" "$G/$n.ckpt"
    v="lhd_ckpt diff exit $RC: $(first '^(SAME|DIFFER)')"
    run python3 "$SCRIPT_DIR/checks.py" ckpt "$OUT/jump_$R/$n.ckpt" "$G/$n.ckpt"
    [[ $RC == 0 ]] && same="$same $n" || differ="$differ $n"
    res "compare $R $n" "$v; checks.py exit $RC: $(sed 's/^ckpt: //' "$LAST")"
  done
  res "compare $R" "DIFFER at${differ:- none}; SAME at${same:- none}"
done
# B resumes from the jumped cycle-20 state and runs to cycle 40
drv "$CT" "$OUT/B/top_lean/top.dcert" 20 "$D/tb.stim" ckpt-in:"$OUT/jump_product/20.ckpt" ckpt-out:"$OUT/B/resumed40.ckpt"
cp "$LAST" "$OUT/B/resumed.trace"
res "resume B from jumped 20" "exit $RC, $(nsteps) STEP lines ($(grep -m1 -oE '^STEP [0-9]+' "$LAST" | cut -d' ' -f2 || true)..$(grep -oE '^STEP [0-9]+' "$LAST" | tail -1 | cut -d' ' -f2 || true))"
drv "$CK" diff "$OUT/B/resumed40.ckpt" "$G/40.ckpt"
v="lhd_ckpt diff exit $RC: $(first '^(SAME|DIFFER)')"
run python3 "$SCRIPT_DIR/checks.py" ckpt "$OUT/B/resumed40.ckpt" "$G/40.ckpt"
res "compare resumed 40" "$v; checks.py exit $RC: $(sed 's/^ckpt: //' "$LAST")"
run python3 "$SCRIPT_DIR/checks.py" steps "$OUT/B/resumed.trace" "$OUT/B/poweron.trace" 20 39
res "outputs resumed vs B 20..39" "exit $RC: $(sed 's/^steps: //' "$LAST")"
run python3 "$SCRIPT_DIR/checks.py" steps "$OUT/B/resumed.trace" "$OUT/A/poweron.trace" 20 39
res "outputs resumed vs A 20..39" "exit $RC: $(sed 's/^steps: //' "$LAST")"
# the product's run from power-on, split by prefix into A's part, B's part and the monitor
for R in product product_wrong; do
  PR="$OUT/$R/states"; mkdir -p "$PR"
  ats=(); for j in $(seq 0 40); do ats+=("ckpt-at:$j:$PR/$j.ckpt"); done
  drv "$CT" "$OUT/$R/top_lean/top.dcert" 40 "$D/tb.stim" "${ats[@]}"
  cp "$LAST" "$OUT/$R/poweron.trace"
  res "run $R" "exit $RC, $(ls "$PR" | wc -l) states, $(nsteps) STEP lines"
  run python3 "$SCRIPT_DIR/checks.py" ok "$OUT/$R/poweron.trace" 0 39
  res "ok on the testbench $R" "exit $RC: $(sed 's/^ok: //' "$LAST")"
  run python3 "$SCRIPT_DIR/checks.py" product "$OUT/$R/top_lean/top_io.json" "$PR" "$OUT/A/top_lean/top_io.json" "$AS" \
    "$OUT/B/top_lean/top_io.json" "$G" "$OUT/jump_$R/transport.json" 0 40
  res "product split $R" "exit $RC: $(sed 's/^product: //' "$LAST")"
done

# ------------------------------------------------------------------------------------------
step "checks (written before the first run; each reads the first RESULT line with its label)"
expect1() { # <label> <ERE the RESULT text must match>: prints PASS or FAIL; returns 0 on PASS
  local lab=$1 re=$2 line n="" t=""
  line=$(awk -v p="RESULT $lab: " 'index($0, p) == 1 { print NR "\t" substr($0, length(p) + 1); exit }' "$L")
  if [[ -n $line ]]; then n=${line%%$'\t'*}; t=${line#*$'\t'}; fi
  if [[ -n $line ]] && grep -qE -- "$re" <<< "$t"; then
    printf 'PASS  %s  (run.log:%s)\n' "$lab" "$n"; return 0
  fi
  printf 'FAIL  %s: expected /%s/; %s\n' "$lab" "$re" "${n:+line $n: $t}${n:-no RESULT line}"; return 1
}
NP=0; NF=0
expect() { if expect1 "$@" >> "$CHK"; then NP=$((NP + 1)); else NF=$((NF + 1)); fi; }
# the grader's own selftest: a fake log, five cases, each with its known answer
LREAL=$L; L="$OUT/.selftest.log"; printf 'RESULT x: SAME 3/0\nRESULT y: DIFFER at 30\n' > "$L"
st=0; sn=0
for c in "x|SAME|0" "x|DIFFER|1" "z|.*|1" "y|^DIFFER at 30\$|0" "x|^SAME\$|1"; do
  IFS='|' read -r lab re want <<< "$c"
  got=0; expect1 "$lab" "$re" > /dev/null || got=1
  sn=$((sn + 1)); [[ $got == "$want" ]] && st=$((st + 1))
done
L=$LREAL
res "expect selftest" "$st ok / $((sn - st)) FAIL (cases examined: $sn)"

SAME_CK='lhd_ckpt diff exit 0: SAME; checks\.py exit 0: design same, cycle same \(CYC\), flops 3/0, words 12/0, first difference none$'
expect "expect selftest"   '^5 ok / 0 FAIL \(cases examined: 5\)$'
expect "checks selftest"   '^exit 0: checks selftest: 20 ok / 0 FAIL \(cases examined: 20\)$'
expect "designs"           '^wrote A\.prp B\.prp product\.prp product_wrong\.prp tb\.stim$'
for n in A B product product_wrong; do expect "certificate $n" '^written, [0-9]+ bytes, sha256 [0-9a-f]{16}$'; done
for n in A B product; do expect "inputs $n" "^input ordinals \{0: 'clock', 1: 'din', 2: 'en', 3: 'reset', 4: 'wa', 5: 'we'\} match$"; done
# step 2
expect "Transport.lean"    "^lean exit 0; 2 of 2 theorems print 'does not depend on any axioms'$"
expect "prove product k:1 init:reset" '^exit 0: RESULT: INVARIANT PROVED \(k=1\) -- '
expect "theorem file"      "^exit 0: embedded design [0-9]+ bytes sha256 ([0-9a-f]{16}) vs the product's certificate [0-9]+ bytes sha256 \1: identical; .*template, exactly: yes; .*output 0 is \['ok'\]: yes; no assumptions \(asmOut none, asmIn \[\]\): yes; k = 1: yes; .*flop widths \(6\) = spec fws: yes"
expect "certificate reader" '^exit 0: readCertChecked: ok; 6 flops, 2 memories, 1 outputs, [0-9]+ nodes$'
expect "certificate reader on non-certificate" '^exit 1: readCertChecked: error '
expect "lean theorem"      '^exit 0, [0-9]+ s wall; axioms: \[propext, Classical\.choice, Quot\.sound, verdict\._native\.native_decide\.ax_1_1\]$'
for k in badproof wrongdesign baddesign; do
  expect "lean corrupted $k" '^exit [1-9][0-9]*, [0-9]+ s wall; first error: .*native_decide.*false'
done
expect "prove product_wrong k:1 init:reset" '^exit 1: RESULT: NOT K-INDUCTIVE \(step\) at k = 1'
expect "prove product_wrong base k:25 init:reset" '^exit 1: RESULT: COUNTEREXAMPLE \(base, replayed\): .* at cycle 1 '
# step 3
expect "run A"             '^exit 0, 41 states \(cycles 0\.\.40\), 40 STEP lines$'
for R in product product_wrong; do
  expect "jump $R" '^exit 0: counters \{"A_states_read": 41, "B_steps_simulated": 0, "R_evaluations": 4, "monitor_steps": 40, "next_state_crosschecks": 40\}$'
  expect "jump $R pending" '^an increment is pending at cycles \[[0-9]'
  for n in 10 20 30 40; do
    expect "import $R $n" "^import exit 0: OK: imported cycle $n: 3 flops, 1 memories \(instance .t.\); check exit 0: OK: cycle $n, 3 flops, 1 memories, 0 ext sections$"
  done
done
expect "import without flop inc" '^exit 1: REFUSED: flop [0-9]+ \(inc\): regs\.json has no key .t\.inc.'
expect "simulator commands in step 3" '^cert_trace on A 1, on B 0, on a product 0; lhd sim 0; sym_verify 0$'
# step 4
expect "B from power-on"   '^exit 0, 41 states \(cycles 0\.\.40\), 40 STEP lines$'
for n in 10 20 30 40; do expect "compare product $n" "^${SAME_CK/CYC/$n}"; done
expect "compare product"   '^DIFFER at none; SAME at 10 20 30 40$'
expect "compare product_wrong" '^DIFFER at [0-9]'
expect "resume B from jumped 20" '^exit 0, 20 STEP lines \(20\.\.39\)$'
expect "compare resumed 40" "^${SAME_CK/CYC/40}"
expect "outputs resumed vs B 20..39" '^exit 0: 20 lines compared, 0 differ, 0 missing$'
expect "outputs resumed vs A 20..39" '^exit 1: 20 lines compared, [1-9][0-9]* differ, 0 missing'
expect "run product"       '^exit 0, 41 states, 40 STEP lines$'
expect "ok on the testbench product" '^exit 0: 40 STEP lines read, ok = 1 on 40, ok = 0 on 0$'
expect "product split product" '^exit 0: 41 cycles, 1230 values compared, 0 differ, first difference none$'
expect "run product_wrong" '^exit 0, 41 states, 40 STEP lines$'
expect "ok on the testbench product_wrong" '^exit 1: 40 STEP lines read, ok = 1 on [0-9]+, ok = 0 on [1-9]'
expect "product split product_wrong" '^exit 0: 41 cycles, 1230 values compared, 0 differ, first difference none$'

say ""; rel < "$CHK" >> "$L"; cat "$CHK"
V="xpatch: $([[ $NF == 0 ]] && echo PASS || echo FAIL) ($NP of $((NP + NF)) checks passed)"
say "$V"; echo "$V"
exit $(( NF != 0 ))
