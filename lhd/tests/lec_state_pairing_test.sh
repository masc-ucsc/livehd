#!/bin/bash
# 2f-lec tier-2 uncertain state correspondence: semdiff full-match pairs are
# injected as UNCERTAIN, PROVEN only via the self-certifying inductive proof,
# REFUTED confirmed pair-free (drop-all + retry once), bounded bmc PASS never
# claimed with pairs applied, and a PASS persists entity-keyed pair hints that
# warm runs replay without the signature pass.
LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || LHD=./lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: lhd binary not found"; exit 1; }
W="${TEST_TMPDIR:-/tmp/lec_state_pairing_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*"; exit 1; }

# Two-stage pipeline; the impl clone renames every register.
cat > "$W/ref.prp" <<'EOF'
mod dut(d:u8) -> (q:u8@[1]) {
  reg ra:u8 = 0
  reg rb:u8 = 0
  q = rb
  rb = ra
  ra = d
}
EOF
sed 's/ra/xa/g; s/rb/xb/g' "$W/ref.prp" > "$W/impl.prp"

# ---------------------------------------------------------------------------
# 1. Renamed pipeline, NO match file: tier-2 pairs both flops, the inductive
#    proof self-certifies, and the disclosure names the uncertain pairs.
# ---------------------------------------------------------------------------
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --workdir "$W/wd1" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#1 renamed pipeline should be PROVEN (rc=$RC): $OUT"
echo "$OUT" | grep -q "tier-2 state pairing: 2 uncertain pair(s) injected" || fail "#1 missing injection line: $OUT"
echo "$OUT" | grep -q "PROVEN with 2 uncertain tier-2 pair(s) applied" || fail "#1 missing self-certifying disclosure: $OUT"
grep -q '"pair_hints"' "$W/wd1/formal_cache.json" || fail "#1 pair hint not persisted"
grep -q '"ra", "xa"' "$W/wd1/formal_cache.json" || fail "#1 pair hint content wrong: $(cat "$W/wd1/formal_cache.json")"
echo "PASS: renamed pipeline PROVEN via uncertain tier-2 pairs; pair hint persisted"

# ---------------------------------------------------------------------------
# 2. Warm re-run in the same workdir: the pair hint re-injects the same pair
#    set (same um=[...] key), the verdict cache hits, and the signature pass
#    never runs.
# ---------------------------------------------------------------------------
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --workdir "$W/wd1" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#2 warm re-run should be PROVEN (rc=$RC): $OUT"
echo "$OUT" | grep -q "PROVEN (cache)" || fail "#2 warm run should hit the verdict cache: $OUT"
echo "$OUT" | grep -q "tier-2 state pairing" && fail "#2 warm run must skip the signature pass: $OUT"
echo "PASS: warm run replays the pair hint and hits the cache (no signature pass)"

# ---------------------------------------------------------------------------
# 3. Genuinely different renamed design: pairs apply, BMC refutes, the
#    drop-all pair-free re-solve refutes on its own -> a REAL FAIL.
# ---------------------------------------------------------------------------
sed 's/xb = xa/xb = xa ^ 1/' "$W/impl.prp" > "$W/bad.prp"
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/bad.prp" --workdir "$W/wd3" 2>&1)
RC=$?
[ "$RC" -ne 0 ] || fail "#3 a real difference must FAIL: $OUT"
echo "$OUT" | grep -q "tier-2 confirm (REFUTED under 2 uncertain tier-2 pair(s); dropped all, re-solved pair-free)" \
  || fail "#3 missing the pair-free confirmation disclosure: $OUT"
echo "PASS: real difference still FAILs through the pair-free confirming re-solve"

# ---------------------------------------------------------------------------
# 4. Planted BOGUS (crossed) pair hint on the EQUIVALENT pair: ind goes SAT
#    (distrusted), bmc bounded-proves — and the bounded PASS is SUPPRESSED
#    under uncertain pairs. Honest UNKNOWN, never a wrong verdict.
# ---------------------------------------------------------------------------
mkdir -p "$W/wd4"
cat > "$W/wd4/formal_cache.json" <<'EOF'
{
  "schema": 1,
  "salt": "0000000000000000",
  "verdicts": {},
  "unknowns": {},
  "hints": {},
  "pair_hints": {
    "dut": {"pairs": [["ra", "xb"], ["rb", "xa"]]}
  }
}
EOF
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --workdir "$W/wd4" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#4 suppressed bounded PASS is a non-strict UNKNOWN, clean exit (rc=$RC): $OUT"
echo "$OUT" | grep -q "bounded bmc PASS suppressed under 2 uncertain tier-2 pair(s)" || fail "#4 missing suppression: $OUT"
echo "$OUT" | grep -q "UNKNOWN" || fail "#4 verdict should be UNKNOWN: $OUT"
echo "$OUT" | grep -q "PROVEN equivalent" && fail "#4 a crossed speculative pairing must never PROVE: $OUT"
# Self-heal: the stale hint whose solve did not PASS is dropped, so the next
# run pairs fresh and reaches the true (unbounded) PROVEN.
grep -q '"ra", "xb"' "$W/wd4/formal_cache.json" && fail "#4 stale crossed hint must be cleared: $(cat "$W/wd4/formal_cache.json")"
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --workdir "$W/wd4" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#4 post-heal re-run should be PROVEN (rc=$RC): $OUT"
echo "$OUT" | grep -q "PROVEN with 2 uncertain tier-2 pair(s) applied" || fail "#4 post-heal run should pair fresh and prove: $OUT"
grep -q '"ra", "xa"' "$W/wd4/formal_cache.json" || fail "#4 post-heal run should store the correct hint"
echo "PASS: bogus crossed pair hint ends UNKNOWN (bounded PASS suppressed), self-heals, then PROVEs fresh"

# ---------------------------------------------------------------------------
# 5. Init-value mismatch: the tier-2 precondition refuses the pair and the
#    report says why; the real divergence (the differing reset) still FAILs
#    through the pair-free confirmation.
# ---------------------------------------------------------------------------
sed 's/reg xa:u8 = 0/reg xa:u8 = 1/' "$W/impl.prp" > "$W/init.prp"
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/init.prp" --workdir "$W/wd5" 2>&1)
RC=$?
[ "$RC" -ne 0 ] || fail "#5 differing reset value is a real difference, must FAIL: $OUT"
echo "$OUT" | grep -q "kind/init mismatch" || fail "#5 missing the init-mismatch unpaired reason: $OUT"
echo "PASS: init-mismatch pair refused with reason; the reset difference still FAILs"

# ---------------------------------------------------------------------------
# 6. All names match: zero tier-2 work (the signature pass never runs).
# ---------------------------------------------------------------------------
cp "$W/ref.prp" "$W/same.prp"
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/same.prp" --workdir "$W/wd6" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#6 identical design should be PROVEN (rc=$RC): $OUT"
echo "$OUT" | grep -q "tier-2" && fail "#6 all-names-match must do zero tier-2 work: $OUT"
echo "PASS: all-names-match design does zero tier-2 work"

# ---------------------------------------------------------------------------
# 7. formal.lec.state_pairing=false: the pre-tier-2 behavior. Under `auto` the
#    renamed pair still passes, but only BOUNDED (bmc; ind is gated Unknown by
#    the unmatched cut points) — the unbounded ind PROVEN of #1 is exactly
#    what tier-2 buys. Forcing engine=ind shows the gate directly: UNKNOWN.
# ---------------------------------------------------------------------------
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --set formal.lec.state_pairing=false 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#7 pairing-off auto run should still bounded-pass (rc=$RC): $OUT"
echo "$OUT" | grep -q "tier-2 state pairing" && fail "#7 pairing ran despite formal.lec.state_pairing=false: $OUT"
echo "$OUT" | grep -q "BOUNDED-Proven" || fail "#7 pairing-off verdict should be the bounded bmc PASS: $OUT"
# The witness-carrying Unknown (matched portion differs through the unmatched
# cuts) escalates in the exit policy — a nonzero exit, but NOT a REFUTED.
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --set formal.lec.state_pairing=false --set formal.engine=ind 2>&1)
RC=$?
[ "$RC" -ne 0 ] || fail "#7 ind witness-carrying UNKNOWN escalates (rc=$RC): $OUT"
echo "$OUT" | grep -q "UNKNOWN" || fail "#7 ind with pairing off should gate to UNKNOWN: $OUT"
echo "$OUT" | grep -q "REFUTED (not equivalent)" && fail "#7 must not claim REFUTED through unmatched cuts: $OUT"
echo "$OUT" | grep -q "cut point" || fail "#7 the unmatched cut points should be named: $OUT"
echo "PASS: formal.lec.state_pairing=false keeps the pre-tier-2 behavior (bounded auto / ind-UNKNOWN)"

# ---------------------------------------------------------------------------
# 8. Flat path (formal.lec.hier=false): same pairing + proof, and the PASS
#    stores an entity-keyed pair hint there too.
# ---------------------------------------------------------------------------
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --set formal.lec.hier=false --workdir "$W/wd8" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#8 flat path should be PROVEN (rc=$RC): $OUT"
echo "$OUT" | grep -q "tier-2 state pairing: 2 uncertain pair(s) injected" || fail "#8 missing flat-path injection: $OUT"
echo "$OUT" | grep -q "PROVEN with 2 uncertain tier-2 pair(s) applied" || fail "#8 missing flat-path disclosure: $OUT"
grep -q '"ra", "xa"' "$W/wd8/formal_cache.json" || fail "#8 flat-path pair hint not persisted: $(cat "$W/wd8/formal_cache.json")"
echo "PASS: flat (non-hier) path pairs, proves, and persists the hint"

echo "ALL PASS: lec tier-2 uncertain state correspondence"
exit 0
