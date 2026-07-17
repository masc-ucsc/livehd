#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Design-size gate (memory admission by node count). A flattened design far past
# ~1M nodes is where whole-design ABC synthesis and cvc5 LEC start exhausting host
# memory (a flat XSCore run reached 221 GB). The gate counts the flattened design
# BEFORE any translation and, unlike the RSS sampler, is deterministic and
# host-independent: `pass color` warns, `pass abc` / `lec` refuse with a clear
# override.
#
# The 1M default is impractical to hit in a hermetic test, so the threshold is
# pinned tiny via LIVEHD_LARGE_DESIGN_NODES=1 (0 would disable it). Asserts:
#   1. pass.color WARNS but still succeeds (exit 0) on an over-threshold design
#   2. pass.abc (whole-design flatten) REFUSES: nonzero exit + large-design code
#   3. pass.abc.allow_oversize=true overrides the refusal
#   4. pass.abc per-def (flatten=false) does NOT fire (aggregate is not one unit)
#   5. lec REFUSES with nonzero exit, naming formal.allow_oversize
#   6. formal.allow_oversize=true overrides the refusal (back to PROVEN)
#   7. the DEFAULT threshold does not false-positive on the same tiny design
#
# Hermetic: the small vendored Liberty + a tiny Pyrope fixture, no PDK.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
PRP=inou/prp/tests/pyrope/abc_comb.prp
TOP=abc_comb.abc_comb
W="${TEST_TMPDIR:-/tmp/lhd_size_gate_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lg" --workdir "$W/w1"

# ---------------------------------------------------------------------------
# 1. pass.color: a WARNING, not a failure -- coloring is fine at any size.
# ---------------------------------------------------------------------------
LIVEHD_LARGE_DESIGN_NODES=1 "$LHD" pass color flat --top "$TOP" lg:"$W/lg" \
  --emit diagnostics:"$W/color.jsonl" --workdir "$W/w2" -q --result-json "$W/color.json" \
  || fail "pass.color must WARN, not fail, on a large design"
grep -q '"code":"large-design"' "$W/color.jsonl" \
  || fail "pass.color emitted no large-design warning: $(cat "$W/color.jsonl" 2>/dev/null)"
grep -q '"severity":"warning"' "$W/color.jsonl" \
  || fail "the large-design diagnostic must be a warning in pass.color"

run pass color flat --top "$TOP" lg:"$W/lg" --workdir "$W/w2b"  # (re-color for the abc runs below)

# ---------------------------------------------------------------------------
# 2. pass.abc whole-design flatten: a hard REFUSAL.
# ---------------------------------------------------------------------------
if LIVEHD_LARGE_DESIGN_NODES=1 "$LHD" pass abc --top "$TOP" lg:"$W/lg" \
    --emit-dir lg:"$W/abc_refused" --set abc.library="$LIB" --set abc.flatten=true \
    --emit diagnostics:"$W/abc.jsonl" --workdir "$W/w3" -q --result-json "$W/abc.json" 2>/dev/null; then
  fail "pass.abc accepted an over-threshold whole-design flatten"
fi
grep -q '"code":"large-design"' "$W/abc.jsonl" \
  || fail "pass.abc emitted no large-design refusal: $(cat "$W/abc.jsonl" 2>/dev/null)"
[ -d "$W/abc_refused" ] && [ -n "$(ls -A "$W/abc_refused" 2>/dev/null)" ] \
  && fail "a refused abc run left a partial netlist"

# ---------------------------------------------------------------------------
# 3. allow_oversize overrides the abc refusal.
# ---------------------------------------------------------------------------
LIVEHD_LARGE_DESIGN_NODES=1 run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/abc_forced" \
  --set abc.library="$LIB" --set abc.flatten=true --set abc.allow_oversize=true --workdir "$W/w4"
[ -n "$(ls -A "$W/abc_forced" 2>/dev/null)" ] || fail "abc.allow_oversize=true produced no netlist"

# ---------------------------------------------------------------------------
# 4. per-def abc (flatten=false) must NOT fire: no single whole-design unit.
# ---------------------------------------------------------------------------
LIVEHD_LARGE_DESIGN_NODES=1 run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/abc_perdef" \
  --set abc.library="$LIB" --set abc.flatten=false --workdir "$W/w5"
[ -n "$(ls -A "$W/abc_perdef" 2>/dev/null)" ] || fail "per-def abc was wrongly refused by the size gate"

# ---------------------------------------------------------------------------
# 5+6. lec: a hard REFUSAL naming formal.allow_oversize, then the override.
#      semdiff=none forces the solver path (prove_equal) where the gate lives;
#      otherwise identical designs are dropped structurally before any encode.
# ---------------------------------------------------------------------------
if LIVEHD_LARGE_DESIGN_NODES=1 "$LHD" lec --impl "$PRP" --ref "$PRP" \
    --impl-top "$TOP" --ref-top "$TOP" --set formal.solver=cvc5 --set formal.lec.semdiff=none \
    --workdir "$W/w6" -q --result-json "$W/lec.json" 2>/dev/null; then
  fail "lec accepted an over-threshold design"
fi
grep -q 'allow_oversize' "$W/lec.json" \
  || fail "lec refusal does not name the override flag: $(cat "$W/lec.json" 2>/dev/null)"

LIVEHD_LARGE_DESIGN_NODES=1 run lec --impl "$PRP" --ref "$PRP" \
  --impl-top "$TOP" --ref-top "$TOP" --set formal.solver=cvc5 --set formal.lec.semdiff=none \
  --set formal.allow_oversize=true --workdir "$W/w7"

# ---------------------------------------------------------------------------
# 7. the default (~1M) threshold must not false-positive on this tiny design.
# ---------------------------------------------------------------------------
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/abc_default" \
  --set abc.library="$LIB" --set abc.flatten=true --workdir "$W/w8"
[ -n "$(ls -A "$W/abc_default" 2>/dev/null)" ] || fail "the default threshold false-refused a tiny abc run"

echo "PASS: design-size gate (color warns; abc/lec refuse + override; per-def and default do not fire)"
