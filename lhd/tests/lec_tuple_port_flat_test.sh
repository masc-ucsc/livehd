#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# LEC tuple-leaf <-> flat-bus port correspondence.
#
# Pyrope tuple-typed ports compile to PER-LEAF lgraph ports (`req.a` u4,
# `req.b` u8) while the same design read from SystemVerilog carries ONE flat
# packed port (`req` [11:0]). Strict by-name IO pairing made the two sets
# unrelated free symbols -> false REFUTED (when the other outputs still
# name-matched) or Unknown. prove_equal now detects the shape divergence
# (`base` flat on exactly one side, the other side holding only `base.<leaf>`
# decls whose widths SUM to the flat width) and bridges it: input leaves bind
# to extracts of ONE shared flat symbol, output bundles compare
# concat(leaves) == flat bus. Layout convention under test: the FIRST leaf in
# decl order is the MSB range of the flat bus (SystemVerilog packed-struct
# layout).
#
# Sections:
#   1. positive: leaf.prp vs flat SV twin PROVEN, both --impl/--ref directions
#   2. bmc engine: the same pair PROVEN under formal.engine=bmc
#   3. negatives: swapped output fields REFUTED; off-by-one REFUTED; a WRONG
#      (LSB-first) input layout REFUTED (proves the MSB-first convention is
#      actually exercised, both directions)
#   4. width-sum mismatch: correspondence declined -> UNKNOWN (unmatched cut
#      points reported; no crash, no proof)
#   5. nested: req:(hdr:(a:u1,b:u2), pay:u4) vs a flat 7-bit bus PROVEN
#   6. sequential: bundle ports + a flop cut PROVEN (bmc engine)
#   7. hierarchy: a PROVEN child whose ports diverge leaf<->bus is DESCENDED
#      (0 child collapses), and the parent still ends PROVEN

set -u
LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: lhd binary not found (set LHD=/path/to/lhd)"; exit 1; }
W="${TEST_TMPDIR:-/tmp/lhd_lec_tuple_flat_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# ── designs ─────────────────────────────────────────────────────────────────
cat > "$W/leaf.prp" <<'EOF'
pub comb leaf(req:(a:u4,b:u8)) -> (rsp:(sum:u9,lo:u4)) {
  rsp.sum = req.a + req.b
  rsp.lo  = req.a
}
EOF
# Flat twin. Field order convention: a:u4 declared FIRST = req's TOP 4 bits;
# sum:u9 first = rsp's TOP 9 bits.
cat > "$W/leaf_flat.sv" <<'EOF'
module leaf(input [11:0] req, output [12:0] rsp);
  wire [3:0] a = req[11:8];
  wire [7:0] b = req[7:0];
  assign rsp[12:4] = a + b;
  assign rsp[3:0]  = a;
endmodule
EOF
# Negative: output fields swapped (lo at the MSB range).
cat > "$W/leaf_swap.sv" <<'EOF'
module leaf(input [11:0] req, output [12:0] rsp);
  wire [3:0] a = req[11:8];
  wire [7:0] b = req[7:0];
  assign rsp[12:9] = a;
  assign rsp[8:0]  = a + b;
endmodule
EOF
# Negative: off-by-one in the sum field.
cat > "$W/leaf_off1.sv" <<'EOF'
module leaf(input [11:0] req, output [12:0] rsp);
  wire [3:0] a = req[11:8];
  wire [7:0] b = req[7:0];
  assign rsp[12:4] = a + b + 9'd1;
  assign rsp[3:0]  = a;
endmodule
EOF
# Negative: WRONG input layout (first-declared leaf read from the LSBs).
cat > "$W/leaf_inswap.sv" <<'EOF'
module leaf(input [11:0] req, output [12:0] rsp);
  wire [3:0] a = req[3:0];
  wire [7:0] b = req[11:4];
  assign rsp[12:4] = a + b;
  assign rsp[3:0]  = a;
endmodule
EOF
# Width-sum mismatch: 12-bit rsp vs 13 leaf bits -> correspondence declined.
cat > "$W/leaf_wmis.sv" <<'EOF'
module leaf(input [11:0] req, output [11:0] rsp);
  wire [3:0] a = req[11:8];
  wire [7:0] b = req[7:0];
  assign rsp[11:4] = a + b;
  assign rsp[3:0]  = a;
endmodule
EOF
# Nested bundle: hdr.a = bit 6, hdr.b = bits 5:4, pay = bits 3:0.
cat > "$W/leaf2.prp" <<'EOF'
pub comb leaf2(req:(hdr:(a:u1,b:u2), pay:u4)) -> (o:u7) {
  o = (req.hdr.a << 6) | (req.hdr.b << 4) | req.pay
}
EOF
cat > "$W/leaf2_flat.sv" <<'EOF'
module leaf2(input [6:0] req, output [6:0] o);
  assign o = req;
endmodule
EOF
# Sequential: bundle ports alongside a flop cut.
cat > "$W/leafreg.prp" <<'EOF'
pub mod leafreg(req:(a:u4,b:u8)) -> (rsp:(sum:u9,lo:u4)@[]) {
  reg racc:u9 = 0
  rsp.sum = racc
  rsp.lo  = req.a
  racc = req.a + req.b
}
EOF
cat > "$W/leafreg_flat.sv" <<'EOF'
module leafreg(input clock, input reset, input [11:0] req, output [12:0] rsp);
  reg [8:0] racc;
  always @(posedge clock) begin
    if (reset) racc <= 9'd0;
    else       racc <= req[11:8] + req[7:0];
  end
  assign rsp = {racc, req[11:8]};
endmodule
EOF
# Hierarchy: the child's ports diverge leaf<->bus; the parent's do not.
cat > "$W/par.prp" <<'EOF'
pub comb kid(req:(a:u4,b:u8)) -> (s:u9) {
  s = req.a + req.b
}
pub comb par(x:u12) -> (y:u9) {
  y = kid(req.a=x#[8..=11], req.b=x#[0..=7])
}
EOF
cat > "$W/par.sv" <<'EOF'
module kid(input [11:0] req, output [8:0] s);
  assign s = req[11:8] + req[7:0];
endmodule
module par(input [11:0] x, output [8:0] y);
  kid k(.req(x), .s(y));
endmodule
EOF

# ── compile everything to lg libraries ──────────────────────────────────────
compile() { # src top lgdir
  "$LHD" compile "$W/$1" --top "$2" --emit-dir "lg:$W/$3" --workdir "$W/cw_$3" -q >/dev/null 2>&1 \
    || fail "compile of $1 failed"
}
compile leaf.prp        leaf    lg_leaf_prp
compile leaf_flat.sv    leaf    lg_leaf_sv
compile leaf_swap.sv    leaf    lg_swap
compile leaf_off1.sv    leaf    lg_off1
compile leaf_inswap.sv  leaf    lg_inswap
compile leaf_wmis.sv    leaf    lg_wmis
compile leaf2.prp       leaf2   lg_leaf2_prp
compile leaf2_flat.sv   leaf2   lg_leaf2_sv
compile leafreg.prp     leafreg lg_reg_prp
compile leafreg_flat.sv leafreg lg_reg_sv
compile par.prp         par     lg_par_prp
compile par.sv          par     lg_par_sv

lec() { # tag top impl ref extra...
  local tag="$1" top="$2" impl="$3" ref="$4"; shift 4
  "$LHD" lec --impl "lg:$W/$impl" --ref "lg:$W/$ref" --top "$top" \
    --workdir "$W/lw_$tag" --result-json "$W/$tag.json" "$@" > "$W/$tag.out" 2>&1
  return 0
}
expect_proven() { # tag
  grep -q '"status":"pass"' "$W/$1.json" || fail "$1: expected PROVEN, got: $(tail -2 "$W/$1.out")"
  grep -q "PROVEN equivalent" "$W/$1.out" || fail "$1: no PROVEN line: $(tail -2 "$W/$1.out")"
}
expect_refuted() { # tag
  grep -q '"status":"fail"' "$W/$1.json" || fail "$1: expected REFUTED, got: $(tail -2 "$W/$1.out")"
  grep -q "REFUTED" "$W/$1.out" || fail "$1: no REFUTED line: $(tail -2 "$W/$1.out")"
}

# 1. positive, both directions; the disclosure names the bundles.
lec pos_fwd leaf lg_leaf_prp lg_leaf_sv
expect_proven pos_fwd
grep -q "leaf<->bus port bundle(s): req\[in\] rsp\[out\]" "$W/pos_fwd.out" \
  || fail "pos_fwd: bundle disclosure missing: $(tail -2 "$W/pos_fwd.out")"
lec pos_rev leaf lg_leaf_sv lg_leaf_prp
expect_proven pos_rev
echo "PASS: leaf.prp <-> flat SV twin PROVEN (both directions, bundles disclosed)"

# 2. bmc engine.
lec pos_bmc leaf lg_leaf_prp lg_leaf_sv --set formal.engine=bmc
expect_proven pos_bmc
echo "PASS: bmc engine PROVEN through the bundle correspondence"

# 3. negatives: order/off-by-one must REFUTE (not Unknown).
lec neg_swap leaf lg_leaf_prp lg_swap
expect_refuted neg_swap
lec neg_off1 leaf lg_leaf_prp lg_off1
expect_refuted neg_off1
lec neg_inswap leaf lg_leaf_prp lg_inswap
expect_refuted neg_inswap
lec neg_swap_bmc leaf lg_leaf_prp lg_swap --set formal.engine=bmc
expect_refuted neg_swap_bmc
echo "PASS: swapped fields / off-by-one / LSB-first input layout all REFUTED (MSB-first convention exercised)"

# 4. width-sum mismatch: declined -> UNKNOWN with unmatched cut points, no
#    crash, no proof, no refutation. (Non-strict: inconclusive is a warning.)
lec neg_wmis leaf lg_leaf_prp lg_wmis
grep -q "UNKNOWN" "$W/neg_wmis.out" || fail "wmis: expected UNKNOWN, got: $(tail -2 "$W/neg_wmis.out")"
grep -q "PROVEN equivalent" "$W/neg_wmis.out" && fail "wmis: width-sum mismatch must not PROVE"
grep -q "REFUTED" "$W/neg_wmis.out" && fail "wmis: width-sum mismatch must not REFUTE"
grep -q "cut point(s)" "$W/neg_wmis.out" || fail "wmis: unmatched cut points not reported: $(tail -2 "$W/neg_wmis.out")"
echo "PASS: width-sum mismatch declines the bundle (UNKNOWN + unmatched cut points, no crash)"

# 5. nested bundle.
lec pos_nest leaf2 lg_leaf2_prp lg_leaf2_sv
expect_proven pos_nest
echo "PASS: nested bundle req:(hdr:(a,b),pay) vs flat 7-bit bus PROVEN"

# 6. sequential (bundle ports + flop cut), bmc engine.
lec pos_reg leafreg lg_reg_prp lg_reg_sv --set formal.engine=bmc
expect_proven pos_reg
echo "PASS: sequential bundle twin PROVEN (bmc, flop cut + bundles)"

# 7. hierarchy: the diverged PROVEN child must be descended, not collapsed.
lec pos_hier par lg_par_prp lg_par_sv
expect_proven pos_hier
grep -q "lec\[hier\]: 'par' PROVEN (0 child collapses)" "$W/pos_hier.out" \
  || fail "hier: expected the diverged child DESCENDED (0 child collapses): $(grep "lec\[hier\]" "$W/pos_hier.out")"
echo "PASS: hier driver descends the leaf<->bus diverged proven child (parent PROVEN)"

echo "lec_tuple_port_flat_test: all sections PASS"
