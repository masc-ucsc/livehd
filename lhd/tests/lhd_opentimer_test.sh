#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `lhd pass opentimer` (2opt-freq D): OpenTimer STA on a
# pass.abc tech-mapped module, emitting the machine-readable timing report.
#
#   prp -> lg (O1)
#   pass color synth ; pass abc --emit-dir lg:net      (comb regions)
#   pass opentimer --top <region> lg:net test.lib      (this pass)
#     -> <workdir>/timing.json {kind:"sta", max_delay, critical_pin,
#        critical_src "file:line", endpoints[]} + envelope "qor" member
#   sequential: abc_seq mapped UNCOLORED (single region, native Flops kept) ->
#     opentimer scores it with flops as path boundaries (Q = arrival 0)
#   latch: an 8-bit native latch is a hard boundary, retains its Q bus name,
#     and is never presented to OpenTimer as an unknown combinational cell
#   negative controls: the rebuilt netlist top (region instances are not
#     Liberty cells) and a nonexistent --top must both FAIL
#
# Hermetic: the small vendored Liberty (inou/prp/tests/abc/test.lib).

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
PRP=inou/prp/tests/pyrope/abc_comb.prp
TOP=abc_comb.abc_comb
W="${TEST_TMPDIR:-/tmp/lhd_opentimer_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

# 1. combinational: compile + color + abc tech-map
run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lg" --workdir "$W/w1"
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/w2"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net" --set abc.library="$LIB" --workdir "$W/w3"

# 2. STA on one mapped region: timing.json under --workdir + envelope "qor"
run pass opentimer --top "${TOP}" lg:"$W/net" "$LIB" --workdir "$W/wt"
[ -f "$W/wt/timing.json" ] || fail "no timing.json under --workdir"
grep -q '"kind":"sta"' "$W/wt/timing.json" || fail "timing.json missing kind:sta"
grep -q '"max_delay":' "$W/wt/timing.json" || fail "timing.json missing max_delay"
grep -q '"critical_pin":"g[0-9]*_[A-Za-z0-9]*:' "$W/wt/timing.json" || fail "timing.json missing a gate critical_pin"
grep -q '"critical_src":"[^"]*abc_comb.prp:[0-9]*"' "$W/wt/timing.json" || fail "critical path not source-attributed"
grep -q '"endpoints":\[{' "$W/wt/timing.json" || fail "timing.json missing endpoints"
grep -q '"qor":{"schema_version":1,"kind":"sta"' "$W/r.json" || fail "envelope missing the qor member"

# 3. sequential, flat (uncolored -> single region, native Flops kept):
#    flop-boundary STA must produce a max_delay with zero OT connect errors
SPRP=inou/prp/tests/pyrope/abc_seq.prp
STOP=abc_seq.abc_seq
[ -f "$SPRP" ] || fail "missing fixture $SPRP"
run compile "$SPRP" --top "$STOP" --recipe O1 --emit-dir lg:"$W/slg" --workdir "$W/w4"
run pass abc --top "$STOP" lg:"$W/slg" --emit-dir lg:"$W/snet" --set abc.library="$LIB" --workdir "$W/w5"
"$LHD" pass opentimer --top "${STOP}" lg:"$W/snet" "$LIB" --workdir "$W/wts" \
    -q --result-json "$W/rs.json" 2> "$W/ot_seq.err" || fail "seq opentimer -> $(cat "$W/rs.json")"
grep -q '"max_delay":' "$W/wts/timing.json" || fail "seq timing.json missing max_delay"
if grep -qE '^[WE] ' "$W/ot_seq.err"; then
  fail "OpenTimer warnings/errors on the seq netlist: $(grep -E '^[WE] ' "$W/ot_seq.err" | head -3)"
fi

# 3a. native level-sensitive latch boundary: ABC must leave the latch intact,
# preserve the packed Q bus name across its per-bit PIs, and OpenTimer must cut
# timing at Q rather than reject the native cell. The logic on both sides makes
# all eight Q bits observable by timing (not merely a dead state declaration).
LPRP="$W/latch.prp"
cat > "$LPRP" <<'EOF'
pub mod ot_latch(en:bool, a:u8, b:u8) -> (q:u8@[0]) {
  reg held:u8:[latch=true]
  if en {
    held = a & b
  }
  q = held ^ a
}
EOF
LTOP=ot_latch.ot_latch
run compile "$LPRP" --top ot_latch --recipe O1 --emit-dir lg:"$W/llg" --workdir "$W/wl1"
run pass abc --top "$LTOP" lg:"$W/llg" --emit-dir lg:"$W/lnet" --set abc.library="$LIB" --workdir "$W/wl2"
run compile lg:"$W/lnet" --top "$LTOP" --emit verilog:"$W/latch_net.v" --workdir "$W/wl3"
grep -Eq 'reg( signed)? \[7:0\] held;' "$W/latch_net.v" \
  || fail "ABC latch read-back lost the original 8-bit Q bus name"
"$LHD" pass opentimer --top "$LTOP" lg:"$W/lnet" "$LIB" --workdir "$W/wtl" \
    -q --result-json "$W/rl.json" 2> "$W/ot_latch.err" || fail "latch opentimer -> $(cat "$W/rl.json")"
grep -q '"max_delay":' "$W/wtl/timing.json" || fail "latch timing.json missing max_delay"
if grep -qE '^[WE] ' "$W/ot_latch.err"; then
  fail "OpenTimer warnings/errors on the latch netlist: $(grep -E '^[WE] ' "$W/ot_latch.err" | head -3)"
fi

# 3b. whole-design timing (hier=true): the hierarchical netlist top (wrapper +
#     region instances) is structurally flattened into one scratch module and
#     timed end-to-end — zero OT connect errors, module name = the real top.
"$LHD" pass opentimer --set pass.opentimer.hier=true --top "$TOP" lg:"$W/net" "$LIB" --workdir "$W/wth" \
    -q --result-json "$W/rh.json" 2> "$W/ot_hier.err" || fail "hier=true opentimer -> $(cat "$W/rh.json")"
grep -q "\"module\":\"$TOP\"" "$W/wth/timing.json" || fail "hier timing.json must report the real top name"
grep -q '"max_delay":' "$W/wth/timing.json" || fail "hier timing.json missing max_delay"
if grep -qE '^[WE] ' "$W/ot_hier.err"; then
  fail "OpenTimer warnings/errors on the hier=true run: $(grep -E '^[WE] ' "$W/ot_hier.err" | head -3)"
fi

# 4. default = hier=true: the same hierarchical top times end-to-end with no
#    --set at all (whole-design flattening is the default).
"$LHD" pass opentimer --top "$TOP" lg:"$W/net" "$LIB" --workdir "$W/wd" \
    -q --result-json "$W/rd.json" 2> "$W/ot_def.err" || fail "default (hier=true) opentimer -> $(cat "$W/rd.json")"
grep -q "\"module\":\"$TOP\"" "$W/wd/timing.json" || fail "default timing.json must report the real top name"
grep -q '"max_delay":' "$W/wd/timing.json" || fail "default timing.json missing max_delay"

# 4b. negative control: with explicit hier=false a WRAPPER top -- one that only
#     instantiates region modules, which are not Liberty cells -- must fail
#     (never silent garbage). A single-region def is emitted directly with no
#     wrapper, so force a multi-region split (tiny max_ge) to get one.
run pass color synth --top "$TOP" --set color.max_ge=1 --set color.min_ge=0 lg:"$W/lg" --workdir "$W/wsplit"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_split" --set abc.library="$LIB" --workdir "$W/wa_split"
if "$LHD" pass opentimer --set pass.opentimer.hier=false --top "$TOP" lg:"$W/net_split" "$LIB" --workdir "$W/wn" -q --result-json "$W/rn.json" 2>/dev/null; then
  fail "opentimer hier=false on a wrapper (non-Liberty region Subs) passed; expected failure"
fi

# 5. negative control: a nonexistent --top must fail
if "$LHD" pass opentimer --top "no.such_module" lg:"$W/net" "$LIB" --workdir "$W/wn2" -q --result-json "$W/rn2.json" 2>/dev/null; then
  fail "opentimer with a bogus --top passed; expected failure"
fi

echo "PASS: pass.opentimer STA on mapped regions (comb + flop/latch boundaries) + timing.json/envelope + negative controls"
