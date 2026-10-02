#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for the 2f-fcore verdict cache (lhd.incremental, --workdir
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
# A REFUTED `lhd lec` also writes the counterexample as a Pyrope replay test and
# then BUILDS AND RUNS it -- ~5.5s of host clang per refutation. Nothing here
# reads that replay, so keep the witness (simfail_*.prp/.json is still written)
# and skip only its host build.
  OUT=$(LEC_PHASE_PLAN=1 "$LHD" lec "$@" --set formal.simfail_run=false --top top  --workdir "$WD" 2>&1); RC=$?
}

# 1) Cold run A vs B: nothing cached yet; verdicts get stored.
H --ref "lg:$WORK/A" --impl "lg:$WORK/B"
if [ "$RC" -ne 0 ]; then echo "FAIL: cold A/B rc=$RC (want PROVEN)"; fail=1
elif ! echo "$OUT" | grep -q "lec\[cache\]: 0 hit(s), 3 stored"; then echo "FAIL: cold run did not store 3 verdicts"; fail=1
elif [ ! -f "$WD/formal_cache.json" ]; then echo "FAIL: formal_cache.json not written"; fail=1
elif ! echo "$OUT" | grep -q "\[LEC_FOREST "; then echo "FAIL: cold solver run did not build the clock forest"; fail=1
else echo "ok: cold run stores 3 verdicts"; fi

# 2) Identical re-run: every def settles from the cache, no solver at all.
H --ref "lg:$WORK/A" --impl "lg:$WORK/B"
if [ "$RC" -ne 0 ]; then echo "FAIL: warm A/B rc=$RC"; fail=1
# The order word (top-down vs leaves-first) is formal.lec.hier_order's business;
# what this case pins is that every def settled FROM THE CACHE with no solver.
elif ! echo "$OUT" | grep -qE "3/3 def\(s\) proven .*\(3 via cache, 0 via semdiff, 0 via solver\)"; then
  echo "FAIL: warm re-run not fully cache-settled"; echo "$OUT" | grep "lec\[hier\]"; fail=1
elif echo "$OUT" | grep -q "\[LEC_FOREST "; then
  echo "FAIL: all-hit warm run built a solver-only clock forest"; fail=1
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
H --ref "lg:$WORK/A" --impl "lg:$WORK/B" --set formal.bound=8
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

# 6) Opt-out: lhd.incremental=false runs with no cache at all.
H --ref "lg:$WORK/A" --impl "lg:$WORK/B" --set lhd.incremental=false
if [ "$RC" -ne 0 ]; then echo "FAIL: cache=false rc=$RC"; fail=1
elif echo "$OUT" | grep -q "lec\[cache\]\|PROVEN (cache)"; then echo "FAIL: lhd.incremental=false still used the cache"; fail=1
else echo "ok: lhd.incremental=false disables the cache"; fi

# 7) The strategy hint file section exists and records a winning engine per def.
if ! grep -q '"hints"' "$WD/formal_cache.json"; then echo "FAIL: no hints section persisted"; fail=1
elif ! grep -q '"leaf": {"engine"' "$WD/formal_cache.json"; then echo "FAIL: no strategy hint for leaf"; fail=1
else echo "ok: strategy hints persisted"; fi

# ---- Unknown-attempt ledger (ruling 2026-07-10): an unchanged def that came
# back Unknown skips the re-attempt (still reported inconclusive); a larger
# budget or formal.retry=all re-attempts. Only WITNESS-FREE Unknowns ledger (a
# witnessed partial-miter diff is actionable and exits 1 — re-surfaces every
# run). Fixture: MASKED 64-bit multiply reassociation at timeout=1s — equivalent,
# so no witness, and far beyond a 1s cvc5 budget.
#
# The `& 64'hF0F0...` matters: width alone no longer makes a multiply-rewrite
# miter hard, because `formal.lec.int_blast=auto` (the default since 2026-08-03)
# re-solves a BV give-up as unbounded INTEGERS, where 64 bits is no harder than 8
# — the bare fixture started proving and nothing was ever ledgered as Unknown.
# Masking buries the product under an `iand` lazy refinement, which int-blasting
# does not finish either (measured UNKNOWN on both legs at formal.timeout=30).
cat > "$WORK/H1.v" <<'EOF'
module hard(input [63:0] a, input [63:0] b, input [63:0] c, output [63:0] y); assign y = ((a*b)*c) & 64'hF0F0F0F0F0F0F0F0; endmodule
EOF
cat > "$WORK/H2.v" <<'EOF'
module hard(input [63:0] a, input [63:0] b, input [63:0] c, output [63:0] y); assign y = (a*(b*c)) & 64'hF0F0F0F0F0F0F0F0; endmodule
EOF
C "$WORK/H1.v" --top hard --emit-dir "lg:$WORK/H1" --workdir "$WORK/ch1"
C "$WORK/H2.v" --top hard --emit-dir "lg:$WORK/H2" --workdir "$WORK/ch2"
WDU="$WORK/wdu"; mkdir -p "$WDU"
# formal.min_timeout=1: the int-blast retry leg spends the WHOLE floor on every
# def that comes back Unknown, so the 20s default would add 20s to each U run
# below for no extra coverage — the ledger is what is under test here.
U() { TO=$1; shift; OUT=$("$LHD" lec --ref "lg:$WORK/H1" --impl "lg:$WORK/H2" --top hard \
       --set "formal.timeout=$TO" --set formal.min_timeout=1 \
      "$@" --workdir "$WDU" 2>&1); RC=$?; }

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

# 10) formal.retry=all and a LARGER budget both re-attempt.
U 1 --set formal.retry=all
if echo "$OUT" | grep -q "skipped: known inconclusive"; then echo "FAIL: formal.retry=all still skipped"; fail=1
else echo "ok: formal.retry=all re-attempts"; fi
U 2
if echo "$OUT" | grep -q "skipped: known inconclusive"; then echo "FAIL: larger budget did not re-attempt"; fail=1
else echo "ok: a larger budget re-attempts"; fi

# 11) The digest keys a register's RESET VALUE and a constant-driven output.
#     Neither is a graph node (a constant is a pool pin), and a state cell's
#     forward signature is its name, so both used to be invisible: after
#     R1 == R1 was cached PROVEN, R1 vs R2 (reset 0xA vs 0xB) replayed it.
cat > "$WORK/R1.v" <<'EOF'
module rv(input clk, input rst, input en, input [3:0] d, output [3:0] q, output [3:0] k);
  reg [3:0] acc;
  always @(posedge clk) if (rst) acc <= 4'hA; else if (en) acc <= d;
  assign q = acc;
  assign k = 4'h5;
endmodule
EOF
sed "s/4'hA;/4'hB;/" "$WORK/R1.v" > "$WORK/R2.v"
sed "s/4'h5;/4'h6;/" "$WORK/R1.v" > "$WORK/R3.v"
C "$WORK/R1.v" --top rv --emit-dir "lg:$WORK/R1" --workdir "$WORK/cr1"
C "$WORK/R1.v" --top rv --emit-dir "lg:$WORK/R1b" --workdir "$WORK/cr1b"
C "$WORK/R2.v" --top rv --emit-dir "lg:$WORK/R2" --workdir "$WORK/cr2"
C "$WORK/R3.v" --top rv --emit-dir "lg:$WORK/R3" --workdir "$WORK/cr3"
WDR="$WORK/wdr"; rm -rf "$WDR"; mkdir -p "$WDR"
R() { "$LHD" lec --ref "lg:$WORK/R1" --impl "lg:$WORK/$1" --top rv --workdir "$WDR" 2>&1 | grep "^lec: '" | head -1; }
OUT=$(R R1b)
if ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: reset-value self pair not proven: $OUT"; fail=1; fi
for v in R2 R3; do
  OUT=$(R $v)
  if echo "$OUT" | grep -q "PROVEN equivalent\|verdict cache hit"; then
    echo "FAIL: $v (a different reset value / output constant) replayed a cached verdict: $OUT"; fail=1
  else echo "ok: $v re-proves (digest keys the constant)"; fi
done

# 12) Workdir history must not weaken a verdict (suggestions6 1.4). The
#     strategy hint is keyed by entity NAME, so it survives an impl edit: a
#     bounded BMC pass recorded as the "winning" engine used to make the NEXT
#     impl in the same workdir try bmc first and settle for its bounded pass,
#     although that impl proves unbounded in a fresh workdir. The ref keeps a
#     40-bit packed stage vector whose entry 0 (the input) is never stored;
#     i32 stores only the 4 stages, i40 keeps the reference layout.
cat > "$WORK/dly.v" <<'EOF'
module br_delay(input clk, input rst, input [7:0] in, output [7:0] out, output [39:0] out_stages);
  logic [4:0][7:0] stages;
  assign stages[0] = in;
  for (genvar i = 1; i <= 4; i++) begin : gen_stages
    always_ff @(posedge clk) if (rst) stages[i] <= 0; else stages[i] <= stages[i-1];
  end
  assign out = stages[4];
  assign out_stages = stages;
endmodule
EOF
cat > "$WORK/i32.prp" <<'EOF'
pub mod br_delay(clk:Clock, `in`:U8, rst:U1) -> (out:U8@[], out_stages:U40@[]) {
  reg stages:U32:[reset_pin=rst] = 0
  stages = (stages#[0..<24] << 8) | `in`
  out = stages#[24..<32]
  out_stages = (stages << 8) | `in`
}
EOF
cat > "$WORK/i40.prp" <<'EOF'
pub mod br_delay(clk:Clock, `in`:U8, rst:U1) -> (out:U8@[], out_stages:U40@[]) {
  reg stages:U40:[reset_pin=rst] = 0
  stages#[8..<40] = (stages#[8..<32] << 8) | `in`
  out = stages#[32..<40]
  out_stages = (stages#[8..<40] << 8) | `in`
}
EOF
WDH="$WORK/wdh"; rm -rf "$WDH"; mkdir -p "$WDH"
D() { "$LHD" lec --ref "verilog:$WORK/dly.v" --impl "pyrope:$WORK/$1" --top br_delay --workdir "$2" 2>&1 | grep "^lec: '" | tail -1; }
# LEC_WINDOW_OFF disables slice pairing, so i32 can only reach a bounded pass.
OUT=$(LEC_WINDOW_OFF=1 D i32.prp "$WDH")
if ! echo "$OUT" | grep -q "PASS(6)"; then echo "FAIL: i32 without state windows should be bounded: $OUT"; fail=1
elif ! python3 -c 'import json,sys; h=json.load(open(sys.argv[1])).get("hints",{}); sys.exit(any(v.get("engine")=="bmc" for v in h.values()))' \
    "$WDH/formal_cache.json"; then echo "FAIL: a bounded pass was stored as the winning strategy"; fail=1
else echo "ok: a bounded pass is not recorded as a strategy hint"; fi
OUT=$(D i40.prp "$WDH")
if ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: i40 after i32 in one workdir is not unbounded: $OUT"; fail=1
else echo "ok: the second impl in a shared workdir gets its own unbounded proof"; fi
# A stale bmc hint (an older lhd wrote them) may reorder, never weaken.
WDS="$WORK/wds"; rm -rf "$WDS"; mkdir -p "$WDS"
echo '{"schema":1,"salt":"0","verdicts":{},"unknowns":{},"hints":{"br_delay":{"engine":"bmc","split":"","ms":1}},"pair_hints":{}}' \
  > "$WDS/formal_cache.json"
OUT=$(D i40.prp "$WDS")
if ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: a stale bmc hint weakened the verdict: $OUT"; fail=1
else echo "ok: a stale bmc hint cannot settle for a bounded pass"; fi
# State windows (suggestions6 1.5): the 32-bit shift register pairs with the
# live slice [39:8] of the 40-bit reference flop and proves unbounded.
OUT=$(D i32.prp "$WORK/wdw")
if ! echo "$OUT" | grep -q "PROVEN equivalent.*state window"; then echo "FAIL: i32 did not prove through a state window: $OUT"; fail=1
else echo "ok: narrower flop proves against the live slice of the wider one"; fi

exit $fail
