#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# upass must stay O(N) (a hard invariant), and the loop roll planner walks a
# loop body once per CLASS of names, never once per name. Two shapes that used
# to be quadratic in pass.upass, each compiled at K and 4K names:
#
#  inv  K invariants (`acc ^= x7#[i]`, one input per statement): a per-invariant
#       body walk (constant_field_reads) and a per-name linear search of the
#       invariants (emit_rolled_loop_call) were O(K^2) -- 9.3 s against 0.65 s
#       for K=8000.
#  fin  K final-only outputs (`o7 = x7#[i]`): a must-write proof and a
#       "written before the loop" walk per written name were O(K^2) -- 3.8 s
#       for K=4000.
#  unt  K untyped invariants (`const c7 = x7 ^ 3`, read as `c7#[i]`): a scan
#       of the whole tree per name for its declared type was O(K^2) -- 169 s
#       for K=4000.
#  mlp  K loops in a row, each with its own carry: the "written before this
#       loop" and "read by a later loop domain" walks covered the whole
#       definition once per loop, O(K^2) -- 4.6 s for K=2000.
#
# Linear work grows ~4x from K to 4K; the quadratic forms grew ~13-16x. The
# check allows 10x (the best of three runs each: a loaded machine slows one
# run, rarely all three), and that every loop rolled.
set -euo pipefail
trap 'echo "FAIL at line $LINENO" >&2' ERR
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/loop_roll_linear_$$}"
mkdir -p "$W"

gen() {  # gen <shape> <K> -> $W/<shape><K>.prp
  python3 - "$1" "$2" "$W" <<'PY'
import sys
shape, k, w = sys.argv[1], int(sys.argv[2]), sys.argv[3]
ins = ", ".join(f"x{n}:U8" for n in range(k))
if shape == "mlp":
    outs = ", ".join(f"o{n}:U1" for n in range(k))
    lines = [f"pub comb {shape}{k}({ins}) -> ({outs}) {{"]
    for n in range(k):
        lines += [f"  mut a{n}:U1 = 0", "  for i in 0..<8 {", f"    a{n} ^= x{n}#[i]", "  }", f"  o{n} = a{n}"]
    lines += ["}"]
elif shape == "unt":
    lines = [f"pub comb {shape}{k}({ins}) -> (o:U1) {{"] + [f"  const c{n} = x{n} ^ 3" for n in range(k)]
    lines += ["  mut acc:U1 = 0", "  for i in 0..<8 {"] + [f"    acc ^= c{n}#[i]" for n in range(k)]
    lines += ["  }", "  o = acc", "}"]
elif shape == "inv":
    lines = [f"pub comb {shape}{k}({ins}) -> (o:U1) {{", "  mut acc:U1 = 0", "  for i in 0..<8 {"]
    lines += [f"    acc {'^' if n % 2 == 0 else '|'}= x{n}#[{'i' if n % 2 == 0 else '7 - i'}]" for n in range(k)]
    lines += ["  }", "  o = acc", "}"]
else:
    outs = ", ".join(f"o{n}:U1" for n in range(k))
    lines = [f"pub comb {shape}{k}({ins}) -> ({outs}) {{", "  for i in 0..<8 {"]
    lines += [f"    o{n} = x{n}#[i]" for n in range(k)]
    lines += ["  }", "}"]
open(f"{w}/{shape}{k}.prp", "w").write("\n".join(lines) + "\n")
PY
}

upass_ms() {  # upass_ms <shape> <K>: best pass.upass ms of three compiles (and the loop must roll)
  local name="$1$2" best=""
  for run in 1 2 3; do
    "$LHD" compile "$W/$name.prp" --top "$name" --workdir "$W/w_${name}_$run" --emit-dir lg:"$W/lg_$name" \
      --set lhd.incremental=false --result-json "$W/$name.$run.json" -q >"$W/$name.$run.log" 2>&1 \
      || { cat "$W/$name.$run.log" >&2; exit 1; }
    local ms
    ms=$(python3 -c "import json,sys; print(sum(p['ms'] for p in json.load(open(sys.argv[1]))['phases'] if p['name'] == 'pass.upass'))" \
      "$W/$name.$run.json")
    best=$(python3 -c "import sys; a=sys.argv[1]; print(sys.argv[2] if not a else min(float(a), float(sys.argv[2])))" "$best" "$ms")
  done
  grep -q '__loop' "$W/lg_$name/library.txt" || { echo "FAIL: $name did not roll its loop" >&2; exit 1; }
  echo "$best"
}

for shape in inv fin unt mlp; do
  small=1000
  large=4000
  [ "$shape" != inv ] && small=500 && large=2000
  gen "$shape" "$small"
  gen "$shape" "$large"
  a=$(upass_ms "$shape" "$small")
  b=$(upass_ms "$shape" "$large")
  echo "$shape: pass.upass ${a} ms at K=$small, ${b} ms at K=$large"
  python3 -c "import sys; a, b = float(sys.argv[1]), float(sys.argv[2]); sys.exit(0 if b <= 10 * max(a, 1.0) else 1)" "$a" "$b" \
    || { echo "FAIL: $shape pass.upass grew ${a} -> ${b} ms for 4x the names (super-linear)" >&2; exit 1; }
done
echo "PASS: the loop roll planner stays linear in invariants (typed or not), written names and loops"
