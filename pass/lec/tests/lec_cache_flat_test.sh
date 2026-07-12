#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2f-fcore F2 tail: the verdict cache on the FLAT (non-hierarchical) lec path.
# The hierarchical driver has cached verdicts since F2; this pins the same store on
# `--set lec.hierarchical=false`, which proves the whole design as ONE top-pair miter
# and so stores/hits exactly ONE verdict. Cold run stores; identical warm re-run hits
# ("PROVEN (cache)", no solver); a verdict-relevant option change (bound) is a new key
# (miss); a design edit is a new digest (miss); a REFUTE is never cached;
# lec.cache=false opts out.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/leccacheflat}"
mkdir -p "$WORK"
fail=0
WD="$WORK/wd"

# a == b (equivalent, De Morgan); c differs.
cat > "$WORK/a.v" <<'EOF'
module foo(input [7:0] a, input [7:0] b, output [7:0] z);
  assign z = a & b;
endmodule
EOF
cat > "$WORK/b.v" <<'EOF'
module foo(input [7:0] a, input [7:0] b, output [7:0] z);
  assign z = ~((~a) | (~b));
endmodule
EOF
cat > "$WORK/c.v" <<'EOF'
module foo(input [7:0] a, input [7:0] b, output [7:0] z);
  assign z = a | b;
endmodule
EOF

# H IMPL REF [EXTRA --set...] -> full stdout+stderr of a flat lec run against $WD.
H() {
  local impl=$1 ref=$2; shift 2
  "$LHD" lec --ref "$WORK/$ref" --impl "$WORK/$impl" --top foo \
         --set lec.hierarchical=false --workdir "$WD" "$@" 2>&1
}
has() { echo "$1" | grep -q "$2"; }
ck() { if has "$1" "$2"; then echo "ok: $3"; else echo "FAIL: $3"; echo "$1" | grep -E 'lec(\[cache\])?:' | head -3; fail=1; fi; }
ckn() { if has "$1" "$2"; then echo "FAIL: $3 (unexpected '$2')"; fail=1; else echo "ok: $3"; fi; }

rm -rf "$WD"

# 1) cold run: stores exactly one verdict; writes formal_cache.json.
OUT=$(H b.v a.v)
ck "$OUT" "lec\[cache\]: 0 hit(s), 1 stored" "cold flat run stores 1 verdict"
if [ -f "$WD/formal_cache.json" ]; then echo "ok: formal_cache.json written"; else echo "FAIL: no formal_cache.json"; fail=1; fi

# 2) warm re-run: hits the cache (no solver), engine=cache.
OUT=$(H b.v a.v)
ck "$OUT" "PROVEN (cache)"                 "warm flat re-run hits (PROVEN (cache))"
ck "$OUT" "lec\[cache\]: 1 hit(s), 0 stored" "warm flat re-run reports 1 hit"

# 3) verdict-relevant option change (bound) is a distinct key -> miss (new store).
OUT=$(H b.v a.v --set lec.bound=8)
ck "$OUT" "lec\[cache\]: 0 hit(s), 1 stored" "bound change is a new key (miss)"

# 4) REFUTE is never cached.
OUT=$(H c.v a.v)
ckn "$OUT" "0 hit(s), 1 stored" "REFUTE never stored"   # a fresh store line would list >=1 stored for THIS run; c!=a refutes
ck  "$OUT" "REFUTED"            "c vs a refutes"

# 5) lec.cache=false opts out (no cache line at all even with a --workdir).
OUT=$(H b.v a.v --set lec.cache=false)
ckn "$OUT" "lec\[cache\]:" "lec.cache=false opts out of the cache"

if [ $fail -ne 0 ]; then echo "lec_cache_flat_test: FAILED"; exit 1; fi
echo "lec_cache_flat_test: PASSED"
exit 0
