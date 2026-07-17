#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for the 2f-fcore verdict cache (lec.cache, --workdir
# formal_cache.json). A def-pair whose hierarchical (Merkle) canonical digests
# AND verdict-relevant options match a stored PROVEN record is settled with no
# analysis at all; anything else re-proves. Soundness: only definitive Proven
# verdicts are ever stored (a REFUTE always re-proves), an option change is a
# key change, and the Merkle digest re-proves every ancestor of an edited def
# (conservative v1 — ancestors usually resolve via the semdiff skip).

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/leccache}"; mkdir -p "$WORK"; fail=0

# Build A: 3-level top -> mid -> leaf.
cat > "$WORK/A.v" <<'EOF'
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = a & b; endmodule
module mid (input [7:0] a, input [7:0] b, output [7:0] z); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign z = t ^ 8'hFF; endmodule
module top (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.z(o)); endmodule
EOF
# Build B: leaf rewritten via De Morgan (equivalent, structurally different).
cat > "$WORK/B.v" <<'EOF'
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = ~((~a) | (~b)); endmodule
module mid (input [7:0] a, input [7:0] b, output [7:0] z); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign z = t ^ 8'hFF; endmodule
module top (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.z(o)); endmodule
EOF
# Build B2: like B but the MID is rewritten (equivalent xor form); leaf + top
# byte-identical to B.
cat > "$WORK/B2.v" <<'EOF'
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = ~((~a) | (~b)); endmodule
module mid (input [7:0] a, input [7:0] b, output [7:0] z); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign z = ~t; endmodule
module top (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.z(o)); endmodule
EOF
# Build C: leaf NON-equivalent.
cat > "$WORK/C.v" <<'EOF'
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = a | b; endmodule
module mid (input [7:0] a, input [7:0] b, output [7:0] z); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign z = t ^ 8'hFF; endmodule
module top (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.z(o)); endmodule
EOF
C() { "$LHD" compile "$@" >/dev/null 2>&1; }
C "$WORK/A.v"  --top top --emit-dir "lg:$WORK/A"  --workdir "$WORK/ca"
C "$WORK/B.v"  --top top --emit-dir "lg:$WORK/B"  --workdir "$WORK/cb"
C "$WORK/B2.v" --top top --emit-dir "lg:$WORK/B2" --workdir "$WORK/cb2"
C "$WORK/C.v"  --top top --emit-dir "lg:$WORK/C"  --workdir "$WORK/cc"

WD="$WORK/wd"; mkdir -p "$WD"
H() {  # $1..=extra lhd lec args ; sets RC/OUT ; ONE shared workdir (the cache)
  OUT=$("$LHD" lec "$@" --top top --set lec.hier=true --workdir "$WD" 2>&1); RC=$?
}

# 1) Cold run A vs B: nothing cached yet; verdicts get stored.
H --ref "lg:$WORK/A" --impl "lg:$WORK/B"
if [ "$RC" -ne 0 ]; then echo "FAIL: cold A/B rc=$RC (want PROVEN)"; fail=1
elif ! echo "$OUT" | grep -q "lec\[cache\]: 0 hit(s), 3 stored"; then echo "FAIL: cold run did not store 3 verdicts"; fail=1
elif [ ! -f "$WD/formal_cache.json" ]; then echo "FAIL: formal_cache.json not written"; fail=1
else echo "ok: cold run stores 3 verdicts"; fi

# 2) Identical re-run: every def settles from the cache, no solver at all.
H --ref "lg:$WORK/A" --impl "lg:$WORK/B"
if [ "$RC" -ne 0 ]; then echo "FAIL: warm A/B rc=$RC"; fail=1
elif ! echo "$OUT" | grep -q "3/3 def(s) proven leaves-first (3 via cache, 0 via semdiff, 0 via solver)"; then
  echo "FAIL: warm re-run not fully cache-settled"; echo "$OUT" | grep "lec\[hier\]"; fail=1
else echo "ok: identical re-run is 3/3 via cache"; fi

# 3) Edit ONE def (mid, in B2): leaf is BELOW the edit so its digest is
#    unchanged => cache hit; mid re-proves (solver); top is an ancestor of the
#    edit (Merkle) => key miss, but the unchanged structure resolves via
#    semdiff. No stale verdict may survive for mid/top.
H --ref "lg:$WORK/A" --impl "lg:$WORK/B2"
if [ "$RC" -ne 0 ]; then echo "FAIL: A/B2 rc=$RC (want PROVEN)"; fail=1
elif ! echo "$OUT" | grep -q "lec\[hier\]: 'leaf' PROVEN (cache)"; then echo "FAIL: unchanged leaf below the edit did not hit the cache"; fail=1
elif echo "$OUT" | grep -q "lec\[hier\]: 'mid' PROVEN (cache)"; then echo "FAIL: EDITED mid served from the cache (stale verdict!)"; fail=1
elif echo "$OUT" | grep -q "lec\[hier\]: 'top' PROVEN (cache)"; then echo "FAIL: ancestor top served from the cache despite a child edit (Merkle broken!)"; fail=1
else echo "ok: edit re-proves mid + ancestors; unchanged leaf hits"; fi

# 4) Option change = key change: same designs, different bound => no hits.
H --ref "lg:$WORK/A" --impl "lg:$WORK/B" --set lec.bound=8
if [ "$RC" -ne 0 ]; then echo "FAIL: A/B bound=8 rc=$RC"; fail=1
elif echo "$OUT" | grep -q "PROVEN (cache)"; then echo "FAIL: bound change did not invalidate the key"; fail=1
else echo "ok: an option change misses the cache"; fi

# 5) Soundness: a REFUTE is never served from (or stored into) the cache.
H --ref "lg:$WORK/A" --impl "lg:$WORK/C"
RC1=$RC
H --ref "lg:$WORK/A" --impl "lg:$WORK/C"
if [ "$RC1" -eq 0 ] || [ "$RC" -eq 0 ]; then echo "FAIL: A/C did not refute (rc $RC1/$RC)"; fail=1
elif ! echo "$OUT" | grep -q "lec\[hier\]: 'leaf' REFUTED"; then echo "FAIL: refuted leaf not re-proven on the second run"; fail=1
elif echo "$OUT" | grep -q "'leaf' PROVEN (cache)"; then echo "FAIL: a REFUTED def was served PROVEN from the cache"; fail=1
else echo "ok: refutes always re-prove"; fi

# 6) Opt-out: lec.cache=false runs with no cache at all.
H --ref "lg:$WORK/A" --impl "lg:$WORK/B" --set lec.cache=false
if [ "$RC" -ne 0 ]; then echo "FAIL: cache=false rc=$RC"; fail=1
elif echo "$OUT" | grep -q "lec\[cache\]\|PROVEN (cache)"; then echo "FAIL: lec.cache=false still used the cache"; fail=1
else echo "ok: lec.cache=false disables the cache"; fi

# 7) The strategy hint file section exists and records a winning engine per def.
if ! grep -q '"hints"' "$WD/formal_cache.json"; then echo "FAIL: no hints section persisted"; fail=1
elif ! grep -q '"leaf": {"engine"' "$WD/formal_cache.json"; then echo "FAIL: no strategy hint for leaf"; fail=1
else echo "ok: strategy hints persisted"; fi

# ---- Unknown-attempt ledger (ruling 2026-07-10): an unchanged def that came
# back Unknown skips the re-attempt (still reported inconclusive); a larger
# budget or lec.retry=all re-attempts. Only WITNESS-FREE Unknowns ledger (a
# witnessed partial-miter diff is actionable and exits 1 — re-surfaces every
# run). Fixture: 64-bit multiply reassociation at timeout=1s — equivalent, so
# no witness, and far beyond a 1s cvc5 budget.
cat > "$WORK/H1.v" <<'EOF'
module hard(input [63:0] a, input [63:0] b, input [63:0] c, output [63:0] y); assign y = (a*b)*c; endmodule
EOF
cat > "$WORK/H2.v" <<'EOF'
module hard(input [63:0] a, input [63:0] b, input [63:0] c, output [63:0] y); assign y = a*(b*c); endmodule
EOF
C "$WORK/H1.v" --top hard --emit-dir "lg:$WORK/H1" --workdir "$WORK/ch1"
C "$WORK/H2.v" --top hard --emit-dir "lg:$WORK/H2" --workdir "$WORK/ch2"
WDU="$WORK/wdu"; mkdir -p "$WDU"
U() { TO=$1; shift; OUT=$("$LHD" lec --ref "lg:$WORK/H1" --impl "lg:$WORK/H2" --top hard \
      --set lec.hier=true --set "lec.timeout=$TO" "$@" --workdir "$WDU" 2>&1); RC=$?; }

# 8) First run: Unknown, and the attempt is ledgered (not a verdict).
U 1
if echo "$OUT" | grep -q "skipped: known inconclusive"; then echo "FAIL: first Unknown run already skipped"; fail=1
elif ! grep -q '"unknowns"' "$WDU/formal_cache.json" || ! grep -q '"timeout"' "$WDU/formal_cache.json"; then
  echo "FAIL: Unknown attempt not ledgered"; fail=1
else echo "ok: Unknown attempt ledgered on first run"; fi
RC1=$RC

# 9) Unchanged re-run: skipped, same reported outcome (inconclusive), same rc.
U 1
if ! echo "$OUT" | grep -q "UNKNOWN (skipped: known inconclusive"; then echo "FAIL: unchanged Unknown not skipped"; fail=1
elif ! echo "$OUT" | grep -q "skipped-unknown"; then echo "FAIL: no skipped-unknown count in summary"; fail=1
elif [ "$RC" -ne "$RC1" ]; then echo "FAIL: skip changed the exit code ($RC1 -> $RC)"; fail=1
else echo "ok: unchanged Unknown skips the re-attempt (same outcome)"; fi

# 10) lec.retry=all and a LARGER budget both re-attempt.
U 1 --set lec.retry=all
if echo "$OUT" | grep -q "skipped: known inconclusive"; then echo "FAIL: lec.retry=all still skipped"; fail=1
else echo "ok: lec.retry=all re-attempts"; fi
U 2
if echo "$OUT" | grep -q "skipped: known inconclusive"; then echo "FAIL: larger budget did not re-attempt"; fail=1
else echo "ok: a larger budget re-attempts"; fi

exit $fail
