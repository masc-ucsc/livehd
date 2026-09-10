#!/usr/bin/env bash
# Focused comparison: composed vs pruned.  `composed` measured at exponent ~2.0
# on chain, so the open question is only whether pruning the local context --
# the suspected second source of quadratic cost -- changes the slope.
set -uo pipefail
R=/soe/czeng14/projects/livehd-d3-translation-validation
export PATH="/mada/users/czeng14/.elan/bin:$PATH"
LAKE=/mada/users/czeng14/.elan/bin/lake
cd "$R/formal/lean"
OUT="$R/pass/lean/d3_scaling2.tsv"
TMO="${TMO:-3000}"
printf 'variant\tshape\tn\tcpu_s\tstatus\n' > "$OUT"
timeit() {
  local tf lf cpu st rc
  tf=$(mktemp /tmp/d3t.XXXXXX); lf=$(mktemp /tmp/d3l.XXXXXX)
  /usr/bin/time -f "%U %S" -o "$tf" timeout "$TMO" "$LAKE" env lean "$1" > "$lf" 2>&1
  rc=$?
  cpu=$(tail -1 "$tf" | awk '{printf "%.2f", $1+$2}' 2>/dev/null)
  st=OK
  [ "$rc" -eq 124 ] && st=TIMEOUT
  grep -qE "^probes.*error" "$lf" && st=FAIL
  echo "${cpu:-NA} $st"; rm -f "$tf" "$lf"
}
run() {
  local v sh n f res
  v="$1"; sh="$2"; n="$3"; f="probes/d3c2_${v}_${sh}_${n}.lean"
  python3 "$R/pass/lean/scripts/d3_gen.py" "$n" --variant "$v" --shape "$sh" --out "$f" >/dev/null
  res=$(timeit "$f")
  printf '%s\t%s\t%s\t%s\n' "$v" "$sh" "$n" "$(echo "$res" | tr ' ' '\t')" >> "$OUT"
  echo "$v/$sh n=$n -> $res"
}
for n in 32 64 128 256; do run pruned   chain "$n"; done
for n in 32 64 128;      do run composed far   "$n"; done
for n in 32 64 128;      do run pruned   far   "$n"; done
echo D3-BENCH2-DONE
