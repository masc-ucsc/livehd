#!/usr/bin/env bash
# vref_test.sh: Verilog designs exported by lhd, run on every Lean route and on Verilator, with
# every output compared at every cycle. The cases are in vref/:
#   allops   one design with every operator lhd's Verilog reader emits, at 8/32/65/128 bits, with
#            memories (two write ports, byte enables, a $readmemh ROM, an async reset), two clocks
#            firing apart and an async reset asserted mid-run; inputs from make_inputs.py
#   memrst   a memory cleared by an asynchronous reset (a for loop in the reset arm)
#   signedops  signed operands in wider contexts (sign extension before shifts, sums, products,
#            division, compares and muxes; a mixed signed/unsigned sum is unsigned)
#   twoclkmem  a memory written from two clock domains that fire apart (the commit table)
#   meminit  a memory initialized by an `initial` for loop with nonzero values
#   modvar   `%` with a variable divisor: the certificate export must refuse it (x % 0 is X)
# A route passes a case when its STEP lines equal Verilator's. Verilator, not iverilog: iverilog
# gets division wider than 64 bits wrong.
#
# Environment:
#   LHD        lhd binary (default bazel-bin/lhd/lhd)
#   NATROOT    build-opt.sh ROOT for nat/cert_trace, brought up to date here (default $OUT/natroot)
#   OUT        results folder (default generated/vref)
#   CYCLES     allops cycles (default 64); SEED (default 1)
#   PERDESIGN=1  also allops on the per-design TV and legacy simulators (native builds, slower)
# The last line is `PASS vref_test: ...` or `FAIL vref_test: ...`; the exit code matches.
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
L="$ROOT/formal/lean"
V="$SCRIPT_DIR/vref"
OUT="${OUT:-$ROOT/generated/vref}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
NATROOT="${NATROOT:-$OUT/natroot}"
CYCLES="${CYCLES:-64}"
SEED="${SEED:-1}"
PY="${PYTHON:-python3}"
mkdir -p "$OUT"
bad=0; n=0
fail() { echo "FAIL $*"; bad=$((bad + 1)); }

[[ -x "$LHD" ]] || { echo "FAIL vref_test: no lhd at $LHD"; exit 1; }
command -v verilator > /dev/null || { echo "FAIL vref_test: verilator is not on PATH"; exit 1; }
# build-opt.sh is incremental: a NATROOT already built at this commit costs a few seconds
(cd "$L" && OPT=-O2 ROOT="$NATROOT" bash Futamura_Projection_Scripts/build-opt.sh \
   Futamura_Projection_Scripts/cert_trace.lean) > "$OUT/cert_trace.build.log" 2>&1 \
  || { echo "FAIL vref_test: cert_trace build (log $OUT/cert_trace.build.log)"; exit 1; }
CT="$NATROOT/nat/cert_trace"

ROUTES=("route:ir" "route:ir ir:raw" "route:vc" "route:vc inc:off" "route:vc inc:scan" "route:vc mem:fun"
        "route:vc opt:off" "route:ref" "route:fp" "route:fp loop:full")

# export <case> <top>: lhd's Verilog reader, then the certificate (run in the case folder: a
# $readmemh file name is relative to the working directory)
export_case() {
  local d=$1 top=$2
  (cd "$d" && "$LHD" compile "$top.v" --reader slang --top "$top" --emit-dir lg:lg --workdir cw > compile.log 2>&1 \
     && "$LHD" compile lg:lg --top "$top" --workdir lw --emit-dir lean:lean \
          --set formal.lean.mode=verified_compiler > export.log 2>&1)
}

# verilator_run <case dir>: tb.v + the design, STEP lines into verilator.txt
verilator_run() {
  local d=$1 top=$2
  (cd "$d" && verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style --top-module tb -Mdir vobj \
       tb.v "$top.v" > verilator.build.log 2>&1 && ./vobj/Vtb > verilator.out 2>&1) || return 1
  grep '^STEP' "$d/verilator.out" > "$d/verilator.txt"
}

# first_diff <trace> <verilator trace> <io.json>: the first cycle that differs and its outputs
first_diff() {
  "$PY" -I - "$1" "$2" "$3" <<'EOF'
import json, sys
a = [l.split() for l in open(sys.argv[1])]
b = [l.split() for l in open(sys.argv[2])]
outs = [p["name"] for p in sorted(json.load(open(sys.argv[3]))["outputs"], key=lambda p: p["ordinal"])]
for x, y in zip(a, b):
    if x != y:
        d = [f"{outs[i] if i < len(outs) else i} {u} vs {v}" for i, (u, v) in enumerate(zip(x[2:], y[2:])) if u != v]
        print(f"cycle {x[1]}, {len(d)} outputs: {'; '.join(d[:3])}{' ...' if len(d) > 3 else ''}")
        break
else:
    print(f"{len(a)} cycles vs {len(b)}")
EOF
}

# routes <case dir> <top> <cycles> <edges spec>: every route against verilator.txt
routes() {
  local d=$1 top=$2 cyc=$3 edges=$4 spec tag
  for spec in "${ROUTES[@]}"; do
    tag=${spec//[: ]/_}
    n=$((n + 1))
    (cd "$L" && bash -c "ulimit -s unlimited; '$CT' '$d/lean/$top.dcert' $cyc '$d/stim.txt' $edges $spec") \
      > "$d/ct_$tag.out" 2> "$d/ct_$tag.err"
    grep '^STEP' "$d/ct_$tag.out" > "$d/ct_$tag.txt"
    if [[ ! -s "$d/ct_$tag.txt" ]]; then
      fail "$top $spec: no trace ($(head -c 200 "$d/ct_$tag.err"))"
    elif ! cmp -s "$d/ct_$tag.txt" "$d/verilator.txt"; then
      fail "$top $spec differs from Verilator: $(first_diff "$d/ct_$tag.txt" "$d/verilator.txt" "$d/lean/${top}_io.json")"
    else
      echo "same $top $spec"
    fi
  done
}

# the case's folder under OUT (use a new OUT per run: nothing here deletes old results)
case_dir() { local d; d="$OUT/$1"; mkdir -p "$d"; cp "$V/$1"/* "$d"/; echo "$d"; }

# allops: generated inputs, two clocks (edges file)
d=$(case_dir allops)
if export_case "$d" allops; then
  "$PY" -I "$d/make_inputs.py" "$d/lean/allops_io.json" "$CYCLES" "$SEED" "$d" > "$d/make_inputs.log" 2>&1 \
    || fail "allops: make_inputs.py (log $d/make_inputs.log)"
  if verilator_run "$d" allops; then
    routes "$d" allops "$CYCLES" "edges:file:$d/edges.txt"
  else fail "allops: Verilator (logs in $d)"; fi
else fail "allops: lhd export (logs in $d)"; fi

# signedops and twoclkmem: generated inputs, like allops
for c in signedops twoclkmem; do
  d=$(case_dir $c)
  cp "$V/allops/make_inputs.py" "$d/"
  if export_case "$d" $c; then
    "$PY" -I "$d/make_inputs.py" "$d/lean/${c}_io.json" "$CYCLES" "$SEED" "$d" --top $c \
      > "$d/make_inputs.log" 2>&1 || fail "$c: make_inputs.py (log $d/make_inputs.log)"
    if verilator_run "$d" $c; then
      routes "$d" $c "$CYCLES" "edges:file:$d/edges.txt"
    else fail "$c: Verilator (logs in $d)"; fi
  else fail "$c: lhd export (logs in $d)"; fi
done
d="$OUT/allops"

# allops on the per-design simulators
if [[ "${PERDESIGN:-0}" == 1 && -s "$d/verilator.txt" ]]; then
  cert="$d/lean/allops.dcert"
  "$PY" -I "$ROOT/pass/lean/tools/scripts/make_tv_sim.py" "$cert" --out "$d/tv" > "$d/tv.gen.log" 2>&1
  tvs=$(ls "$d"/tv/*TvSim.lean 2>/dev/null)
  if [[ -n "$tvs" ]] && (cd "$L" && EXTRA_MODULES=Compiler/ReifyGen OPT=-O2 ROOT="$NATROOT" \
        bash Futamura_Projection_Scripts/build-opt.sh "$tvs") > "$d/tv.build.log" 2>&1; then
    n=$((n + 1))
    (cd "$d" && bash -c "ulimit -s unlimited; '$NATROOT/nat/$(basename "$tvs" .lean)' $CYCLES stim.txt edges:file:edges.txt") \
      2> "$d/ct_tv.err" | grep '^STEP' > "$d/ct_tv.txt"
    cmp -s "$d/ct_tv.txt" "$d/verilator.txt" && echo "same allops tv" || fail "allops tv differs from Verilator"
  else fail "allops tv: generate or build (logs in $d)"; fi
  (cd "$d" && "$LHD" compile lg:lg --top allops --workdir legw --emit-dir lean:leg \
     --set formal.lean.emit_cert=false) > "$d/legacy.log" 2>&1
  "$PY" -I "$ROOT/pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py" "$d/leg/allops_Lgraph.lean" \
    --trace --io "$d/lean/allops_io.json" --out "$d/lg" > "$d/lg.gen.log" 2>&1
  lgs=$(ls "$d"/lg/*_LegacyTrace.lean 2>/dev/null)
  if [[ -n "$lgs" ]] && (cd "$L" && EXTRA_MODULES=Translation/LegacyFast OPT=-O2 ROOT="$NATROOT" \
        bash Futamura_Projection_Scripts/build-opt.sh "$lgs") > "$d/lg.build.log" 2>&1; then
    n=$((n + 1))
    (cd "$d" && bash -c "ulimit -s unlimited; '$NATROOT/nat/$(basename "$lgs" .lean)' '$cert' $CYCLES stim.txt edges:file:edges.txt") \
      2> "$d/ct_legacy.err" | grep '^STEP' > "$d/ct_legacy.txt"
    cmp -s "$d/ct_legacy.txt" "$d/verilator.txt" && echo "same allops legacy" || fail "allops legacy differs from Verilator"
  else fail "allops legacy: generate or build (logs in $d)"; fi
fi

# the small cases: their own stim.txt and tb.v, every clock edge every cycle
for c in memrst meminit; do
  d=$(case_dir $c)
  if export_case "$d" $c; then
    if verilator_run "$d" $c; then routes "$d" $c "$(wc -l < "$d/verilator.txt")" edges:all
    else fail "$c: Verilator (logs in $d)"; fi
  else fail "$c: lhd export (logs in $d)"; fi
done

# modvar: the export must refuse, and say why
d=$(case_dir modvar); n=$((n + 1))
if export_case "$d" modvar; then
  fail "modvar: the export accepted % with a variable divisor"
elif grep -q 'rem: only a nonzero CONSTANT divisor' "$d/export.log"; then
  echo "same modvar: refused (% with a variable divisor)"
else
  fail "modvar: the export failed without the expected refusal (log $d/export.log)"
fi

if [[ $bad -eq 0 ]]; then echo "PASS vref_test: $n comparisons, all equal to Verilator"; exit 0; fi
echo "FAIL vref_test: $bad of $n comparisons failed"; exit 1
