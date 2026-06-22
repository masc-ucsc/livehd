#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for the "conditional `reg` write with no covering `else` does NOT
# auto-hold" semantic trap. The HDL idiom "write the register only on some
# condition, otherwise it holds" must lower to an AUTO-HOLD on the unwritten
# path — read the flop q — NOT a don't-care next-state. Two failure modes:
#
#   (a) empty branch:   `if a { r=x } elif b { /*hold*/ } else { r=y }`
#       The empty `elif b {}` body was dropped by the runner's dead-code
#       elimination, sliding `else` into the `b` slot — so r took `y` while b
#       held and held (garbage) otherwise.  Fixed in upass/runner (empty
#       positional if-arm stmts are preserved).
#   (b) pure write:     `if a { r=x } elif b { r=y }`  (no else, r never read)
#       The unwritten next-state fell back to a 65-bit don't-care instead of q.
#       Fixed in upass/tolg (a reg's din shadow holds q on the unwritten path).
#
# This test asserts BOTH: (1) the generated next-state carries NO don't-care
# literal (a regression would re-introduce the `N'sb1???…` balloon), and (2) the
# design is PROVEN equivalent (cvc5) to a Verilog reference with an explicit
# hold on the unwritten path.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_reg_hold_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# no_dontcare <name> : compile $W/<name>.prp to Verilog and assert the output
# carries no don't-care bit literal ('?' only appears in cgen for a `0sb?`
# don't-care const; the Hotmux multi-hot default is 'hx, no '?').
no_dontcare() {
  local n="$1"
  "$LHD" compile "$W/${n}.prp" --top "$n" --emit-dir "verilog:$W/${n}_v/" \
    --workdir "$W/${n}_w" -q >/dev/null 2>&1 \
    || fail "${n}: prp->verilog compile failed"
  local v
  v=$(ls "$W/${n}_v/"*.v 2>/dev/null | head -1)
  [ -n "$v" ] || fail "${n}: no verilog emitted"
  if grep -q '?' "$v"; then
    echo "----- offending lines -----" >&2
    grep -n '?' "$v" >&2
    fail "${n}: don't-care literal in next-state (auto-hold regression)"
  fi
  echo "PASS(${n}): no don't-care in next-state"
}

# prove_hold <name> : prove $W/<name>.prp equivalent to $W/<name>_ref.v (a
# Verilog reference that HOLDS the reg on the unwritten path) via cvc5.
prove_hold() {
  local n="$1"
  "$LHD" compile --reader slang "$W/${n}_ref.v" --top "$n" \
    --emit-dir "lg:$W/${n}_ref/" --workdir "$W/${n}_refw" -q >/dev/null 2>&1 \
    || fail "${n}: reference (.v) compile failed"
  "$LHD" compile "$W/${n}.prp" --top "$n" --emit-dir "lg:$W/${n}_impl/" \
    --set compile.formal.mode=none --workdir "$W/${n}_implw" -q >/dev/null 2>&1 \
    || fail "${n}: impl (.prp) compile failed"
  "$LHD" lec --ref "lg:$W/${n}_ref/" --impl "lg:$W/${n}_impl/" \
    --ref-top "${n}" --impl-top "${n}.${n}" \
    --workdir "$W/${n}_lec" -q --result-json "$W/${n}.json" \
    || fail "${n}: cvc5 lec did NOT prove auto-hold equivalent: $(cat "$W/${n}.json" 2>/dev/null)"
  grep -q '"status":"pass"' "$W/${n}.json" || fail "${n}: lec not pass: $(cat "$W/${n}.json")"
  echo "PASS(${n}): auto-hold == explicit-hold reference (cvc5)"
}

# ── (a) empty `elif` arm with a covering else: the PC stall idiom ──────────────
cat >"$W/pcunit.prp" <<'EOF'
mod pcunit(taken:bool, stall:bool, tgt:u8, nx:u8) -> (pc:u8@[0]) {
  reg pcr:u8 = 0
  if taken {
    pcr = tgt
  } elif stall {
    // hold
  } else {
    pcr = nx
  }
  pc = pcr
}
EOF
cat >"$W/pcunit_ref.v" <<'EOF'
module pcunit(input taken, input stall, input [7:0] tgt, input [7:0] nx,
              output [7:0] pc, input clock, input reset);
  reg [7:0] pcr;
  always @(posedge clock)
    if (reset)       pcr <= 8'b0;
    else if (taken)  pcr <= tgt;
    else if (stall)  pcr <= pcr;
    else             pcr <= nx;
  assign pc = pcr;
endmodule
EOF
no_dontcare pcunit
prove_hold  pcunit

# ── (b) missing else, pure write (never read): the register-file write ────────
cat >"$W/regfile.prp" <<'EOF'
mod regfile(we_a:bool, we_b:bool, adata:u8, bdata:u8) -> (q:u8@[0]) {
  reg r:u8 = 0
  if we_b {
    r = bdata
  } elif we_a {
    r = adata
  }
  q = r
}
EOF
cat >"$W/regfile_ref.v" <<'EOF'
module regfile(input we_a, input we_b, input [7:0] adata, input [7:0] bdata,
               output [7:0] q, input clock, input reset);
  reg [7:0] r;
  always @(posedge clock)
    if (reset)      r <= 8'b0;
    else if (we_b)  r <= bdata;
    else if (we_a)  r <= adata;
  assign q = r;
endmodule
EOF
no_dontcare regfile
prove_hold  regfile

# ── unique-if / match, no else: the Hotmux none-of slot must hold q ───────────
cat >"$W/selreg.prp" <<'EOF'
mod selreg(sel:u2, a:u8, b:u8) -> (q:u8@[0]) {
  reg r:u8 = 0
  unique if sel == 0 {
    r = a
  } elif sel == 1 {
    r = b
  }
  q = r
}
EOF
cat >"$W/selreg_ref.v" <<'EOF'
module selreg(input [1:0] sel, input [7:0] a, input [7:0] b,
              output [7:0] q, input clock, input reset);
  reg [7:0] r;
  always @(posedge clock)
    if (reset) r <= 8'b0;
    else case (sel)
      2'd0:    r <= a;
      2'd1:    r <= b;
      default: r <= r;
    endcase
  assign q = r;
endmodule
EOF
no_dontcare selreg
prove_hold  selreg

echo "ALL PASS: conditional reg writes auto-hold (no don't-care)"
