#!/usr/bin/env bash
# opcorpus_test.sh: the operator corpus (gen_op_corpus.lean: one certificate with the 26 operators
# of the routes' accepted fragment at corner widths and values, and five certificates with the
# operators outside it) on every route.
#   1. engine_check: every route and knob combination, four ways (plain, twice, peeked, reloaded),
#      300 cycles of pseudo-random inputs and edges, outputs and final checkpoints against ref
#      (interpreted, against formal/lean/.olean-dev).
#   2. NATIVE=1: the native cert_trace on every route, 256 cycles of a generated stimulus with two
#      clocks firing apart, every trace against route:ref; with PERDESIGN=1 also the corpus's TV
#      simulator (make_tv_sim.py, a native build). Then the refusals: Sub, Div, LT and GT are
#      refused by ir, vc and fp with a message naming the operator (ref runs them); Const as a node
#      runs on ref and ir and is refused by vc and fp.
#
# Environment: OUT (default generated/opcorpus), NATROOT (build-opt.sh ROOT for nat/cert_trace,
# brought up to date here; default $OUT/natroot), NATIVE, PERDESIGN, PYTHON (default python3), ENGINE_CHECK=0 skips step 1.
# The last line is `PASS opcorpus_test: ...` or `FAIL opcorpus_test: ...`; the exit code matches.
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
L="$ROOT/formal/lean"
OUT="${OUT:-$ROOT/generated/opcorpus}"
NATROOT="${NATROOT:-$OUT/natroot}"
PY="${PYTHON:-python3}"
mkdir -p "$OUT"
bad=0; n=0
fail() { echo "FAIL $*"; bad=$((bad + 1)); }

(cd "$L" && LEAN_PATH="$PWD/.olean-dev" lean --run "$SCRIPT_DIR/gen_op_corpus.lean" "$OUT") > "$OUT/gen.log" 2>&1 \
  || { cat "$OUT/gen.log"; echo "FAIL opcorpus_test: gen_op_corpus.lean"; exit 1; }
grep -q ' 26 operators' "$OUT/gen.log" || { cat "$OUT/gen.log"; echo "FAIL opcorpus_test: the corpus lost an operator"; exit 1; }
cert="$OUT/opcorpus.dcert"

if [[ "${ENGINE_CHECK:-1}" == 1 ]]; then
  n=$((n + 1))
  (cd "$L" && LEAN_PATH="$PWD/.olean-dev" lean --run "$SCRIPT_DIR/engine_check.lean" 300 "$cert") > "$OUT/engine_check.log" 2>&1
  if grep -q '^PASS engine' "$OUT/engine_check.log"; then echo "ok engine_check: $(tail -1 "$OUT/engine_check.log")"
  else fail "engine_check: $(grep -m3 FAIL "$OUT/engine_check.log" | cut -c1-200)"; fi
fi

if [[ "${NATIVE:-0}" == 1 ]]; then
  # build-opt.sh is incremental: a NATROOT already built at this commit costs a few seconds
  (cd "$L" && OPT=-O2 ROOT="$NATROOT" bash Futamura_Projection_Scripts/build-opt.sh \
     Futamura_Projection_Scripts/cert_trace.lean) > "$OUT/cert_trace.build.log" 2>&1 \
    || { echo "FAIL opcorpus_test: cert_trace build (log $OUT/cert_trace.build.log)"; exit 1; }
  # 256 cycles: corner values on the first 8, then random; clock 1 fires on about 3 cycles in 4
  "$PY" -I - "$OUT" <<'EOF'
import random, sys
out = sys.argv[1]; random.seed(7); N = 256
widths = [8, 8, 65, 65, 3, 8, 1, 2, 16, 2, 64, 8, 1, 2, 1]
def corner(w, k): m = (1 << w) - 1; return [0, 1, m, 1 << (w - 1), (1 << (w - 1)) - 1, m - 1, 2, 3][k % 8] if w > 1 else k % 2
with open(f"{out}/stim.txt", "w") as f:
    f.write("stim v1\n")
    for k in range(N):
        for i, w in enumerate(widths):
            v = corner(w, k + i) if k < 8 else random.getrandbits(w)
            if i == 12: v = 1 if k in (0, 1, 100, 101, 102) else 0   # rst: at the start and mid-run
            if v: f.write(f"at {k} in {i} {w} {v}\n")
with open(f"{out}/edges.txt", "w") as f:
    f.write("edges v1\n")
    for k in range(N):
        f.write(f"at {k} 1 {1 if k < 2 or random.random() < 0.75 else 0}\n")
EOF
  for spec in "route:ref" "route:ir" "route:ir ir:raw" "route:vc" "route:vc inc:off" "route:vc inc:scan" \
              "route:vc mem:fun" "route:vc opt:off" "route:fp" "route:fp loop:full" "route:fp share:off"; do
    tag=${spec//[: ]/_}
    (cd "$L" && bash -c "ulimit -s unlimited; '$NATROOT/nat/cert_trace' '$cert' 256 '$OUT/stim.txt' edges:file:$OUT/edges.txt $spec") \
      2> "$OUT/ct_$tag.err" | grep '^STEP' > "$OUT/ct_$tag.txt"
    [[ "$spec" == route:ref ]] && continue
    n=$((n + 1))
    if [[ ! -s "$OUT/ct_$tag.txt" ]]; then fail "native $spec: no trace ($(head -c 200 "$OUT/ct_$tag.err"))"
    elif cmp -s "$OUT/ct_$tag.txt" "$OUT/ct_route_ref.txt"; then echo "same native $spec"
    else fail "native $spec differs from route:ref at $(diff "$OUT/ct_$tag.txt" "$OUT/ct_route_ref.txt" | grep -m1 '^<' | cut -d' ' -f2-3)"; fi
  done
  [[ $(wc -l < "$OUT/ct_route_ref.txt") -eq 256 ]] || fail "native route:ref: $(wc -l < "$OUT/ct_route_ref.txt") of 256 cycles"
  # the operators outside the fragment
  printf 'stim v1\nat 0 in 0 8 7\nat 0 in 1 8 3\nat 1 in 0 8 255\n' > "$OUT/stim2.txt"
  for c in sub div lt gt const; do
    for r in ref ir vc fp; do
      n=$((n + 1))
      (cd "$L" && bash -c "ulimit -s unlimited; '$NATROOT/nat/cert_trace' '$OUT/refuse_$c.dcert' 2 '$OUT/stim2.txt' edges:all route:$r") \
        > "$OUT/refuse_${c}_$r.out" 2>&1
      rc=$?
      steps=$(grep -c '^STEP' "$OUT/refuse_${c}_$r.out")
      if [[ $r == ref || ( $c == const && $r == ir ) ]]; then
        [[ $rc -eq 0 && $steps -eq 2 ]] && echo "ran refuse_$c on $r" || fail "refuse_$c on $r: exit $rc, $steps cycles (it must run)"
      elif [[ $rc -eq 0 || $steps -ne 0 ]]; then
        fail "refuse_$c on $r: exit $rc, $steps cycles (it must be refused)"
      elif [[ $c != const ]] && ! grep -qi "Op_$c\b\|Op_${c^^}\b\|Op_${c^}\b" "$OUT/refuse_${c}_$r.out"; then
        fail "refuse_$c on $r: refused without naming the operator: $(head -c 150 "$OUT/refuse_${c}_$r.out")"
      else
        echo "refused refuse_$c on $r"
      fi
    done
  done
  cmp -s <(grep '^STEP' "$OUT/refuse_const_ref.out") <(grep '^STEP' "$OUT/refuse_const_ir.out") \
    || fail "refuse_const: ir differs from ref"
  if [[ "${PERDESIGN:-0}" == 1 ]]; then
    n=$((n + 1))
    "$PY" -I "$ROOT/pass/lean/tools/scripts/make_tv_sim.py" "$cert" --out "$OUT/tv" > "$OUT/tv.gen.log" 2>&1
    tvs=$(ls "$OUT"/tv/*TvSim.lean 2>/dev/null)
    if [[ -n "$tvs" ]] && (cd "$L" && EXTRA_MODULES=Compiler/ReifyGen OPT=-O2 ROOT="$NATROOT" \
          bash Futamura_Projection_Scripts/build-opt.sh "$tvs") > "$OUT/tv.build.log" 2>&1; then
      (cd "$OUT" && bash -c "ulimit -s unlimited; '$NATROOT/nat/$(basename "$tvs" .lean)' 256 stim.txt edges:file:edges.txt") \
        2> "$OUT/ct_tv.err" | grep '^STEP' > "$OUT/ct_tv.txt"
      cmp -s "$OUT/ct_tv.txt" "$OUT/ct_route_ref.txt" && echo "same tv" || fail "tv differs from route:ref"
    else fail "tv: generate or build (logs in $OUT)"; fi
  fi
fi

if [[ $bad -eq 0 ]]; then echo "PASS opcorpus_test: $n checks"; exit 0; fi
echo "FAIL opcorpus_test: $bad of $n checks failed"; exit 1
