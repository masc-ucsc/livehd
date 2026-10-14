#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# No up-front design-size gate (owner ruling 2026-10-14). pass.abc and lec used
# to refuse a flattened design over ~1M nodes (LIVEHD_LARGE_DESIGN_NODES) unless
# pass.abc.allow_oversize / formal.allow_oversize was set. Both options and both
# gates are gone: every design is ATTEMPTED, and memory exhaustion is caught by
# the memory backstop (RLIMIT_AS, the per-region admission) and reported as
# TIMEOUT-memory instead of a crash. Only pass.color still WARNS about a large
# flattened design, since that is advice, not a refusal.
#
# The threshold is pinned tiny via LIVEHD_LARGE_DESIGN_NODES=1, so this small
# design is "over" it. Asserts:
#   1. pass.color WARNS but still succeeds (exit 0) on an over-threshold design
#   2. pass.abc whole-design flatten is ATTEMPTED: a netlist, no large-design refusal
#   3. pass.abc.allow_oversize is REJECTED as a usage error (exit 2), both spellings
#   4. pass.abc per-def (flatten=false) maps the same design
#   5. lec over the threshold is ATTEMPTED and proves it (PROVEN-full, exit 0)
#   6. formal.allow_oversize is REJECTED as a usage error (exit 2)
#   7. the DEFAULT threshold run maps the same tiny design
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

# rejected <tag> <args...>: the run must be a usage error (exit 2) whose
# envelope names the removed allow_oversize flag, and it must emit nothing.
rejected() {
  local tag="$1"
  shift
  "$LHD" "$@" -q --result-json "$W/$tag.json" 2>/dev/null
  local rc=$?
  [ "$rc" -eq 2 ] || fail "$tag: a removed allow_oversize option must be a usage error (exit 2), got rc=$rc: $(cat "$W/$tag.json" 2>/dev/null)"
  grep -q '"class":"usage"' "$W/$tag.json" || fail "$tag: not a usage-class error: $(cat "$W/$tag.json" 2>/dev/null)"
  grep -q 'allow_oversize' "$W/$tag.json" || fail "$tag: the error does not name allow_oversize: $(cat "$W/$tag.json" 2>/dev/null)"
}

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

run compile "$PRP" --top "$TOP" --emit-dir lg:"$W/lg" --workdir "$W/w1"

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
# 2. pass.abc whole-design flatten over the threshold: ATTEMPTED, not refused.
# ---------------------------------------------------------------------------
LIVEHD_LARGE_DESIGN_NODES=1 "$LHD" pass abc --top "$TOP" lg:"$W/lg" \
  --emit-dir lg:"$W/abc_flat" --set synth.liberty="$LIB" --set abc.flatten=true \
  --emit diagnostics:"$W/abc.jsonl" --workdir "$W/w3" -q --result-json "$W/abc.json" \
  || fail "pass.abc refused an over-threshold whole-design flatten: $(cat "$W/abc.json" 2>/dev/null)"
[ -n "$(ls -A "$W/abc_flat" 2>/dev/null)" ] || fail "the over-threshold whole-design flatten produced no netlist"
! grep -q '"code":"large-design"' "$W/abc.jsonl" 2>/dev/null \
  || fail "pass.abc still gates on design size: $(cat "$W/abc.jsonl")"

# ---------------------------------------------------------------------------
# 3. the removed override is rejected, not silently accepted.
# ---------------------------------------------------------------------------
rejected abc_long pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/abc_rejected" \
  --set synth.liberty="$LIB" --set pass.abc.allow_oversize=true --workdir "$W/w4"
rejected abc_short pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/abc_rejected" \
  --set synth.liberty="$LIB" --set abc.allow_oversize=true --workdir "$W/w4b"
[ -z "$(ls -A "$W/abc_rejected" 2>/dev/null)" ] || fail "a rejected pass.abc run published a netlist"

# ---------------------------------------------------------------------------
# 4. per-def abc (flatten=false) maps the same over-threshold design.
# ---------------------------------------------------------------------------
LIVEHD_LARGE_DESIGN_NODES=1 run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/abc_perdef" \
  --set synth.liberty="$LIB" --set abc.flatten=false --workdir "$W/w5"
[ -n "$(ls -A "$W/abc_perdef" 2>/dev/null)" ] || fail "per-def abc produced no netlist"

# ---------------------------------------------------------------------------
# 5. lec over the threshold: ATTEMPTED and proven. semdiff=none forces the
#    solver path (prove_equal), where the old gate lived; otherwise identical
#    designs are dropped structurally before any encode. effort=proof: the
#    default sampled effort would only report TESTED-sampled.
# ---------------------------------------------------------------------------
LIVEHD_LARGE_DESIGN_NODES=1 "$LHD" lec --impl "$PRP" --ref "$PRP" \
  --impl-top "$TOP" --ref-top "$TOP" --set formal.lec.semdiff=none --set formal.effort=proof \
  --workdir "$W/w6" -q --result-json "$W/lec.json" 2>/dev/null
rc=$?
[ "$rc" -eq 0 ] || fail "lec refused or failed an over-threshold design (rc=$rc): $(cat "$W/lec.json" 2>/dev/null)"
grep -q '"verdict":"proven-full"' "$W/lec.json" \
  || fail "lec of a design against itself is not PROVEN-full: $(cat "$W/lec.json" 2>/dev/null)"

# ---------------------------------------------------------------------------
# 6. the removed formal override is rejected, not silently accepted.
# ---------------------------------------------------------------------------
rejected lec_override lec --impl "$PRP" --ref "$PRP" \
  --impl-top "$TOP" --ref-top "$TOP" --set formal.lec.semdiff=none \
  --set formal.allow_oversize=true --workdir "$W/w7"

# ---------------------------------------------------------------------------
# 7. the default threshold run maps the same tiny design.
# ---------------------------------------------------------------------------
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/abc_default" \
  --set synth.liberty="$LIB" --set abc.flatten=true --workdir "$W/w8"
[ -n "$(ls -A "$W/abc_default" 2>/dev/null)" ] || fail "the default-threshold abc run produced no netlist"

echo "PASS: no design-size gate (color warns; abc/lec attempt an over-threshold design; allow_oversize is rejected)"
