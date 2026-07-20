#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Tuple-typed PORT call-site coverage. A tuple-typed port (`req:(a:u4,b:u8)`)
# flattens to dotted leaf ports (`req.a`, `req.b`) on the Sub instance; the
# call site must materialize the tuple actual as dotted NAMED actuals for
# every Sub-bound callee form, and the multi-output result must be readable
# through the dotted path. Guards three fixes:
#
#  (1) upass.runner: a NAMED tuple actual (`leafm(req=t)`) re-emits its
#      already-computed leaf expansion even when the call has no unnamed
#      actual (the re-emit gate was `becomes_sub && any_unnamed`), and the
#      same re-emit fires for a COMB callee kept as a Sub under the default
#      inline:false (slang-generated Pyrope declares everything `pub comb`,
#      so comb-Sub calls are the hierarchy-recompile path).
#  (2) upass.tolg: a dot-form read of a multi-output instance result
#      (`r.rsp.sum` — a flat all-const tuple_get index chain) joins the
#      indices with '.' and matches the Sub's dotted output name (FULL path
#      only; `r.sum` stays an error when the output is `rsp.sum`).
#  (3) inou.cgen: instance connections escape dotted port names
#      (`.\req.a (sig)`) like the port declarations already did — the raw
#      `.req.a(sig)` was illegal Verilog.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_tuple_port_callsite_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# Golden reference for every parent below: out = x + y (zero-extended), out2 = x.
cat >"$W/gold.v" <<'EOF'
module parent(input [3:0] x, input [7:0] y, output [8:0] out, output [3:0] out2);
  assign out  = {1'b0, {4'b0, x}} + {1'b0, y};
  assign out2 = x;
endmodule
EOF

lec_proven() { # <name> <prp>
  "$LHD" lec --impl "$2" --ref "$W/gold.v" --top parent --set formal.solver=cvc5 \
    --workdir "$W/lec_$1" -q --result-json "$W/lec_$1.json" \
    || fail "$1: lec run failed: $(cat "$W/lec_$1.json" 2>/dev/null)"
  grep -q '"status":"pass"' "$W/lec_$1.json" || fail "$1: lec not PROVEN: $(cat "$W/lec_$1.json")"
}

# ── (a) mod callee + NAMED tuple actual ───────────────────────────────────────
cat >"$W/mod_named.prp" <<'EOF'
pub mod leafm(req:(a:u4, b:u8)) -> (rsp:(sum:u9, lo:u4)@[0]) {
  rsp.sum = req.a + req.b
  rsp.lo  = req.a
}
pub mod parent(x:u4, y:u8) -> (out:u9@[0], out2:u4@[0]) {
  const mytup = (const a = x, const b = y)
  const r = leafm(req=mytup)
  out  = r["rsp.sum"]
  out2 = r["rsp.lo"]
}
EOF
"$LHD" compile "$W/mod_named.prp" --top parent --workdir "$W/wa" -q \
  || fail "mod + NAMED tuple actual did not compile"
lec_proven mod_named "$W/mod_named.prp"
echo "PASS: mod callee + named tuple actual (req=t) compiles and is cvc5-PROVEN"

# ── (b) COMB callee (default inline:false → Sub): named AND positional ────────
cat >"$W/comb_named.prp" <<'EOF'
pub comb leaf(req:(a:u4, b:u8)) -> (rsp:(sum:u9, lo:u4)) {
  rsp.sum = req.a + req.b
  rsp.lo  = req.a
}
pub comb parent(x:u4, y:u8) -> (out:u9, out2:u4) {
  const mytup = (const a = x, const b = y)
  const r = leaf(req=mytup)
  out  = r["rsp.sum"]
  out2 = r["rsp.lo"]
}
EOF
"$LHD" compile "$W/comb_named.prp" --top parent --workdir "$W/wb1" -q \
  || fail "comb + NAMED tuple actual did not compile"
lec_proven comb_named "$W/comb_named.prp"
cat >"$W/comb_pos.prp" <<'EOF'
pub comb leaf(req:(a:u4, b:u8)) -> (rsp:(sum:u9, lo:u4)) {
  rsp.sum = req.a + req.b
  rsp.lo  = req.a
}
pub comb parent(x:u4, y:u8) -> (out:u9, out2:u4) {
  const mytup = (const a = x, const b = y)
  const r = leaf(mytup)
  out  = r["rsp.sum"]
  out2 = r["rsp.lo"]
}
EOF
"$LHD" compile "$W/comb_pos.prp" --top parent --workdir "$W/wb2" -q \
  || fail "comb + POSITIONAL tuple actual did not compile"
lec_proven comb_pos "$W/comb_pos.prp"
echo "PASS: comb callee kept as a Sub takes named and positional tuple actuals (cvc5-PROVEN)"

# ── (c) dot-form multi-output read r.rsp.sum ─────────────────────────────────
cat >"$W/dot_read.prp" <<'EOF'
pub mod leafm(req:(a:u4, b:u8)) -> (rsp:(sum:u9, lo:u4)@[0]) {
  rsp.sum = req.a + req.b
  rsp.lo  = req.a
}
pub mod parent(x:u4, y:u8) -> (out:u9@[0], out2:u4@[0]) {
  const mytup = (const a = x, const b = y)
  const r = leafm(mytup)
  out  = r.rsp.sum
  out2 = r.rsp.lo
}
EOF
"$LHD" compile "$W/dot_read.prp" --top parent --workdir "$W/wc" -q \
  || fail "dot-form output read r.rsp.sum did not compile"
lec_proven dot_read "$W/dot_read.prp"
echo "PASS: dot-form multi-output instance read (r.rsp.sum) compiles and is cvc5-PROVEN"

# A PARTIAL leaf name must stay an error (full-path match only): the output is
# `rsp.sum`, so `r.sum` names no output.
cat >"$W/dot_bad.prp" <<'EOF'
pub mod leafm(req:(a:u4, b:u8)) -> (rsp:(sum:u9, lo:u4)@[0]) {
  rsp.sum = req.a + req.b
  rsp.lo  = req.a
}
pub mod parent(x:u4, y:u8) -> (out:u9@[0], out2:u4@[0]) {
  const mytup = (const a = x, const b = y)
  const r = leafm(mytup)
  out  = r.sum
  out2 = r.lo
}
EOF
if "$LHD" compile "$W/dot_bad.prp" --top parent --workdir "$W/wcbad" -q >"$W/bad.json" 2>&1; then
  fail "partial output name r.sum unexpectedly compiled (must be a full-path no-output error)"
fi
grep -q "no output named" "$W/bad.json" || fail "r.sum failed for the wrong reason: $(cat "$W/bad.json")"
echo "PASS: partial output name (r.sum for rsp.sum) is still rejected"

# ── (e) tuple-literal actual with LOCAL-computed field values ─────────────────
# The field values are locals/temps (bit-selects of an input), not input-port
# refs. Constprop only records a runtime slot→ref carrier when the field ref
# has NO ST bundle (an input port); a local/temp carries a trivial-scalar
# bundle, so the carrier was dropped and the tuple-actual expansion failed with
# `fcall-unknown-arg`. The runner now backfills those carriers after each
# tuple_add (record_runtime_tuple_slot_refs). This is the exact shape the
# slang reader emits at every struct-port call site.
cat >"$W/gold_local.v" <<'EOF'
module p2(input [7:0] fi, output [7:0] oo);
  assign oo = fi;
endmodule
EOF
cat >"$W/local_fields.prp" <<'EOF'
pub comb c2(req:(hi:u4, lo:u4)) -> (o:u8) {
  o = (req.hi << 4) | req.lo
}
pub comb p2(fi:u8) -> (oo:u8) {
  const u = c2(req=(hi=fi#[4..=7], lo=fi#[0..=3]))
  oo = u
}
EOF
"$LHD" compile "$W/local_fields.prp" --top p2 --workdir "$W/we1" -q \
  || fail "comb + tuple literal with LOCAL-computed field values did not compile"
"$LHD" lec --impl "$W/local_fields.prp" --ref "$W/gold_local.v" --top p2 --set formal.solver=cvc5 \
  --workdir "$W/lec_local" -q --result-json "$W/lec_local.json" \
  || fail "local-fields lec run failed: $(cat "$W/lec_local.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/lec_local.json" || fail "local-fields lec not PROVEN: $(cat "$W/lec_local.json")"
cat >"$W/local_fields2.prp" <<'EOF'
pub comb c2(req:(hi:u4, lo:u4)) -> (o:u8) {
  o = (req.hi << 4) | req.lo
}
pub comb p2(fi:u8) -> (oo:u8) {
  const vh = fi#[4..=7]
  const vl = fi#[0..=3]
  const u = c2(req=(hi=vh, lo=vl))
  oo = u
}
EOF
"$LHD" compile "$W/local_fields2.prp" --top p2 --workdir "$W/we2" -q \
  || fail "comb + tuple literal with LOCAL const field values did not compile"
"$LHD" lec --impl "$W/local_fields2.prp" --ref "$W/gold_local.v" --top p2 --set formal.solver=cvc5 \
  --workdir "$W/lec_local2" -q --result-json "$W/lec_local2.json" \
  || fail "local-const-fields lec run failed: $(cat "$W/lec_local2.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/lec_local2.json" || fail "local-const-fields lec not PROVEN: $(cat "$W/lec_local2.json")"
echo "PASS: tuple literal with local-computed field values expands (comb callee, cvc5-PROVEN)"

# ── (d) cgen: dotted instance-connection port names are escaped ───────────────
"$LHD" compile "$W/comb_named.prp" --top parent --emit-dir verilog:"$W/ev" --workdir "$W/wd" -q \
  || fail "verilog emit of the comb hierarchy failed"
PARENT_V=$(grep -l "^module" "$W/ev"/*.v | xargs grep -l '\.\\req\.a ' | head -1)
[ -n "$PARENT_V" ] || fail "no emitted .v carries an escaped instance connection .\\req.a : $(ls "$W/ev")"
grep -q '\.req\.a(' "$W/ev"/*.v && fail "raw (unescaped) .req.a( connection still emitted"
if command -v iverilog >/dev/null 2>&1; then
  iverilog -g2012 -o /dev/null "$W/ev"/*.v || fail "iverilog -g2012 rejects the emitted hierarchy"
  echo "PASS: emitted hierarchy verilog parses (iverilog -g2012, escaped dotted ports)"
else
  echo "PASS: instance connections escape dotted ports (iverilog not present, grep-checked)"
fi

echo "PASS: all tuple-port call-site regressions"
