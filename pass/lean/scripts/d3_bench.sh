#!/usr/bin/env bash
# Direction 3 scaling benchmark: naive vs composed proof, two dependency shapes.
# CPU seconds (user+sys), so a loaded box does not distort the exponent.
set -uo pipefail
R=/soe/czeng14/projects/livehd-d3-translation-validation
# systemd-run --user does not inherit the interactive PATH, so `lake` must be
# absolute or every measurement silently reports ~0 seconds.
export PATH="/mada/users/czeng14/.elan/bin:$PATH"
LAKE=/mada/users/czeng14/.elan/bin/lake
cd "$R/formal/lean"
OUT="${OUT:-$R/pass/lean/d3_scaling.tsv}"
TMO="${TMO:-5400}"
printf 'variant\tshape\tn\tcpu_s\tstatus\n' > "$OUT"

timeit() {  # writes CPU seconds to stdout; args: file
  local tf; tf=$(mktemp /tmp/d3t.XXXXXX)
  local lf; lf=$(mktemp /tmp/d3l.XXXXXX)
  /usr/bin/time -f "%U %S" -o "$tf" timeout "$TMO" "$LAKE" env lean "$1" > "$lf" 2>&1
  local rc=$?
  local cpu; cpu=$(tail -1 "$tf" | awk '{printf "%.2f", $1+$2}' 2>/dev/null)
  local st=OK
  if [ "$rc" -eq 124 ]; then st=TIMEOUT
  elif grep -qE "^probes.*error" "$lf"; then st=FAIL; fi
  echo "${cpu:-NA} $st"
  rm -f "$tf" "$lf"
}

read -r BT BS < <(cat > probes/d3_base.lean <<'B'
import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
B
timeit probes/d3_base.lean)
printf 'baseline\t-\t0\t%s\t%s\n' "$BT" "$BS" >> "$OUT"
echo "baseline ${BT}s $BS"

run() {
  local v sh n f res
  v="$1"; sh="$2"; n="$3"
  f="probes/d3b_${v}_${sh}_${n}.lean"
  python3 "$R/pass/lean/scripts/d3_gen.py" "$n" --variant "$v" --shape "$sh" --out "$f" >/dev/null
  res=$(timeit "$f")
  printf '%s\t%s\t%s\t%s\n' "$v" "$sh" "$n" "$(echo "$res" | tr ' ' '\t')" >> "$OUT"
  echo "$v/$sh n=$n -> $res"
}

for n in 32 64 128 256 512;                    do run naive    chain "$n"; done
for n in 32 64 128 256 512 1024 2048 4096;     do run composed chain "$n"; done
for n in 32 64 128 256 512 1024;               do run composed far   "$n"; done
echo D3-BENCH-DONE
