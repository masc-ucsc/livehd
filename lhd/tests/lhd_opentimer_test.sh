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
grep -q '"critical_pin":"g[0-9]*_[A-Za-z0-9_]*__n[0-9]*:' "$W/wt/timing.json" || fail "timing.json missing a gate critical_pin"
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

# 4a. Cross-region pure-wiring order. occurrence forward order is only
# topological inside one definition: the SRA producer below can land in an
# earlier sibling region than the mapped gates that consume it, while the
# result-side Sext remains in the other region. Tracker population must defer
# the consumer until the producer identity exists, rather than minting an
# `n$sra_*` timing root or reading a zero-width boundary pin.
XPRP="$W/cross_region.prp"
cat >"$XPRP" <<'EOF'
pub mod ot_cross_region(a:s8, b:s8) -> (y:s8@[0]) {
  const wide:s64 = a
  const shifted  = wide >> 11
  y = shifted + b
}
EOF
XTOP=ot_cross_region.ot_cross_region
run compile "$XPRP" --top ot_cross_region --emit-dir lg:"$W/xlg" --workdir "$W/xw1"
run pass color synth --top "$XTOP" lg:"$W/xlg" --set color.max_ge=1 --set color.min_ge=0 --workdir "$W/xw2"
run pass abc --top "$XTOP" lg:"$W/xlg" --emit-dir lg:"$W/xnet" --set abc.library="$LIB" --workdir "$W/xw3"
grep -q '"regions":2' "$W/r.json" || fail "cross-region fixture did not split into two mapped regions"
"$LHD" pass opentimer --top "$XTOP" lg:"$W/xnet" "$LIB" --workdir "$W/xwt" \
    -q --result-json "$W/xr.json" 2> "$W/ot_cross.err" || fail "cross-region opentimer -> $(cat "$W/xr.json")"
grep -q '"max_delay":' "$W/xwt/timing.json" || fail "cross-region timing.json missing max_delay"
if grep -qE '^[WE] ' "$W/ot_cross.err"; then
  fail "OpenTimer warnings/errors on cross-region wiring: $(grep -E '^[WE] ' "$W/ot_cross.err" | head -3)"
fi

# 4aa. Dense ABC input splitter driven by a parent constant. The child maps a
# 300-bit adder and therefore instantiates __livehd_abc_input_bits_300; at this
# occurrence its input resolves all the way to CONST_NODE. Constants are real
# zero-arrival timing leaves, and each splitter SRA must retain its one-bit
# output arity without inventing an `invalid_*` bus net.
CSPR="$W/const_splitter.prp"
cat >"$CSPR" <<'EOF'
mod dense(a:u300) -> (y:u300@[0]) {
  y = a + 1
}

pub mod ot_const_splitter() -> (y:u300@[0]) {
  y = dense(a=0).y
}
EOF
CSTOP=const_splitter.ot_const_splitter
run synth "$CSPR" --top ot_const_splitter --workdir "$W/csw" --emit-dir lg:"$W/csnet" \
    --set synth.liberty="$LIB" --set color.max_ge=1 --set color.min_ge=0
"$LHD" tool tree lg:"$W/csnet" --top "$CSTOP" >"$W/cs.tree" \
    || fail "could not inspect constant-splitter mapped hierarchy"
grep -q '__livehd_abc_input_bits_300' "$W/cs.tree" || fail "constant-splitter fixture did not instantiate dense input helper"
grep -q '"kind":"sta"' "$W/csw/synth/timing.json" || fail "constant-splitter timing.json missing STA report"

# 4ab. A native divider is an intentional ABC black-box boundary, not an
# unmapped cell that should make whole-design STA fail. Its output starts a new
# zero-arrival segment while mapped input/output-side cones remain timed.
DPRP="$W/div_boundary.prp"
cat >"$DPRP" <<'EOF'
pub mod ot_div_boundary(a:u16, b:u8) -> (y:u16@[0]) {
  y = (a / (b | 1)) + 3
}
EOF
run synth "$DPRP" --top ot_div_boundary --workdir "$W/dw" --emit-dir lg:"$W/dnet" \
    --set synth.liberty="$LIB"
grep -q '"div_blackbox":1' "$W/r.json" || fail "divider fixture was not reported as one ABC blackbox"
grep -q '"kind":"sta"' "$W/dw/synth/timing.json" || fail "divider-boundary timing.json missing STA report"

# 4ac. A packed-array assignment around a sliced memory read creates a 64-bit
# Concat lane over an internal 65-bit Set_mask carrier. The source Get_mask is
# the lane-width boundary; ABC's native-boundary readback must preserve that
# low-64-bit cast instead of reconnecting the raw carrier and handing
# OpenTimer a malformed over-wide lane.
CPRP="$W/concat_lane_fit.prp"
cat >"$CPRP" <<'EOF'
pub mod ot_concat_lane(clk:u1, addr:u1, a:u64) -> (y:u64@[0]) {
  reg mem:[2]u64:[clock_pin=ref clk]
  mem[addr] = a
  mut read:u64 = 0sb?
  read#[0..=63] = mem[addr]#[0..=63]
  mut lane:[1]u64 = 0
  lane[0] = read
  y = lane[0]
}
EOF
run synth "$CPRP" --top ot_concat_lane --workdir "$W/cfw" --emit-dir lg:"$W/cfnet" \
    --set synth.liberty="$LIB" --set lhd.incremental=false
grep -q '"kind":"sta"' "$W/cfw/synth/timing.json" || fail "Concat-lane width-boundary timing.json missing STA report"

# 4b. negative control: with explicit hier=false a WRAPPER top -- one that only
#     instantiates region modules, which are not Liberty cells -- must fail
#     (never silent garbage). A single-region def is emitted directly with no
#     wrapper, so force a multi-region split (tiny max_ge) to get one.
run pass color synth --top "$TOP" --set color.max_ge=1 --set color.min_ge=0 lg:"$W/lg" --workdir "$W/wsplit"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_split" --set abc.library="$LIB" --workdir "$W/wa_split"

# 4b. --stats keeps the whole-design critical path above and adds exactly one
# structured row per mapped color. The cold ABC run rebuilt every row, and the
# pretty renderer prints those same rows one per physical line.
"$LHD" pass opentimer --top "$TOP" lg:"$W/net_split" "$LIB" --workdir "$W/wstats_cold" \
    --stats -q --result-json "$W/rstats_cold.json" 2>"$W/ot_stats_cold.err" \
    || fail "cold opentimer stats -> $(cat "$W/rstats_cold.json" 2>/dev/null)"
cold_colors=$(grep -o '"resynth":1' "$W/rstats_cold.json" | wc -l | tr -d ' ')
[ "$cold_colors" -ge 2 ] || fail "opentimer JSON did not report every split color: $(cat "$W/rstats_cold.json")"
"$LHD" pass opentimer --top "$TOP" lg:"$W/net_split" "$LIB" --workdir "$W/wstats_pretty" \
    --stats --diag-fmt pretty -q >"$W/ot_stats.pretty" 2>"$W/ot_stats_pretty.err" \
    || fail "pretty opentimer stats failed"
[ "$(grep -c '^  sta\[stats\]:' "$W/ot_stats.pretty")" = "$cold_colors" ] \
  || fail "pretty opentimer stats are not one line per JSON color: $(cat "$W/ot_stats.pretty")"
[ "$(grep -c 'resynth=1$' "$W/ot_stats.pretty")" = "$cold_colors" ] \
  || fail "cold opentimer color rows did not all say resynth=1: $(cat "$W/ot_stats.pretty")"

# Rebuild the same colored input through the same ABC workdir: every color is a
# cache hit, but OpenTimer must still report every one and carry resynth=0.
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_split_hit" --set abc.library="$LIB" \
    --workdir "$W/wa_split" --stats
"$LHD" pass opentimer --top "$TOP" lg:"$W/net_split_hit" "$LIB" --workdir "$W/wstats_hit" \
    --stats -q --result-json "$W/rstats_hit.json" 2>"$W/ot_stats_hit.err" \
    || fail "incremental opentimer stats -> $(cat "$W/rstats_hit.json" 2>/dev/null)"
[ "$(grep -o '"resynth":0' "$W/rstats_hit.json" | wc -l | tr -d ' ')" = "$cold_colors" ] \
  || fail "incremental opentimer omitted colors or lost resynth=0: $(cat "$W/rstats_hit.json")"
[ "$(grep -o '"resynth":1' "$W/rstats_hit.json" | wc -l | tr -d ' ')" = 0 ] \
  || fail "incremental opentimer incorrectly marked a cache-hit color resynth=1"

if "$LHD" pass opentimer --set pass.opentimer.hier=false --top "$TOP" lg:"$W/net_split" "$LIB" --workdir "$W/wn" -q --result-json "$W/rn.json" 2>/dev/null; then
  fail "opentimer hier=false on a wrapper (non-Liberty region Subs) passed; expected failure"
fi

# 4c. A design carrying a PROPERTY MARKER must still time. A runtime bit-range
# select (`a#[lo..=hi]`) materializes an `lgassert` Sub, and a user assert an
# `fproperty` one; both are recognized primitives, not hardware. pass.partition
# rebuilds the instance without cloning the marker's module declaration, so it
# reaches OpenTimer unbound -- an empty type name and no body -- and the pass
# used to refuse the WHOLE design over it ("whole-design timing hit black-box
# ''"). That took out every cva6 synthesis run and any design with one runtime
# bit-range select. A Sub that drives NOTHING is on no timing path.
MARK="$W/marker.prp"
cat > "$MARK" <<'EOF'
mod marker_sta(a:u8, b:u8, sel:u3) -> (o:u8@[1]) {
  reg r:u8 = 0
  r = (a & b) ^ (a#[0..=sel] + 1)
  o = r
}
EOF
MTOP=marker_sta.marker_sta
run compile "$MARK" --top marker_sta --recipe O1 --emit-dir lg:"$W/mlg" --workdir "$W/mw1"
run pass color synth --top "$MTOP" lg:"$W/mlg" --workdir "$W/mw2"
run pass abc --top "$MTOP" lg:"$W/mlg" --emit-dir lg:"$W/mnet" --set abc.library="$LIB" --workdir "$W/mw3"
run pass opentimer --top "$MTOP" lg:"$W/mnet" "$LIB" --workdir "$W/mw4"
grep -q '"kind":"sta"' "$W/mw4/timing.json" \
  || fail "a design with a runtime bit-range select produced no STA report: $(cat "$W/mw4/timing.json" 2>/dev/null)"

# 5. negative control: a nonexistent --top must fail
if "$LHD" pass opentimer --top "no.such_module" lg:"$W/net" "$LIB" --workdir "$W/wn2" -q --result-json "$W/rn2.json" 2>/dev/null; then
  fail "opentimer with a bogus --top passed; expected failure"
fi

echo "PASS: pass.opentimer STA on mapped regions (comb + flop/latch boundaries) + timing.json/envelope + negative controls"
