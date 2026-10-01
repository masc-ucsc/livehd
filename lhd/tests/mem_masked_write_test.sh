#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# MEMORY PARTIAL WRITES lower to WRITE-MASKED ports.
#
# `mem[a]#[lo..<hi] = v` used to become a read-modify-write: an old-value read
# port, a set_mask, and a whole-entry write. upass.tolg now lowers it to one
# write port whose enable is a lane vector (the Memory cell's `wensize`), so:
#   * no old-value read port is minted (a 2-write, 1-read memory stays 1rd_2wr);
#   * the lane width is the largest one every constant mask is made of (byte
#     writes: wensize 2), and a runtime position takes single-bit lanes;
#   * the cell's last-port-wins lanes merge several partial writes of one entry
#     in one cycle, in every ordering -- the read-modify-write once lost all but
#     the last lane under ordering="old";
#   * an inline reg array (a reset value) forwards per lane to a same-cycle
#     read (cgen used to refuse lanes with a collision matrix there).
# The proof is gated by a MUTANT golden that keeps only the last lane of a
# same-entry pair (on a reset array alone: known contents, so the same-cycle
# read shows the lost lane at once, and small enough to refute in seconds):
# it must refute, or the proof checks nothing about lanes.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

cat > "$W/mw.prp" <<'EOF'
pub mod mw(`reset`:Reset, we0:Bool, we1:Bool, a:U2, b:U2, x:U8, y:U8, k:U4, bv:U1, ra:U2)
  -> (qp:U16@[0], qo:U16@[0], qr:U16@[0], qs:U16@[0]) {
  reg P:[4]U16 = nil
  reg O:[4]U16:[ordering="old"] = nil
  reg R:[4]U16 = nil
  reg S:[4]U16 = 0
  if we0 {
    P[a]#[0..<8] = x
    O[a]#[0..<8] = x
    R[a]#[k] = bv
    S[a]#[0..<8] = x
  }
  if we1 {
    P[b]#[8..<16] = y
    O[b]#[8..<16] = y
    S[b]#[8..<16] = y
  }
  qp = P[ra]
  qo = O[ra]
  qr = R[ra]
  qs = S[ra]
}
EOF

# The golden: nonblocking part selects merge per bit; `qp`/`qr`/`qs` read after
# the writes (ordering "program"), `qo` reads the committed entry.
cat > "$W/gold.v" <<'EOF'
module mw(input clock, input reset, input we0, input we1, input [1:0] a, input [1:0] b, input [7:0] x, input [7:0] y,
          input [3:0] k, input bv, input [1:0] ra,
          output [15:0] qp, output [15:0] qo, output [15:0] qr, output [15:0] qs);
  reg [15:0] P[0:3];
  reg [15:0] O[0:3];
  reg [15:0] R[0:3];
  reg [15:0] S[0:3];
  integer i;
  always @(posedge clock) begin
    if (we0) P[a][7:0] <= x;
    if (we0) R[a][k] <= bv;
    if (we1) P[b][15:8] <= y;
    if (we0) O[a][7:0] <= x;
    if (we1) O[b][15:8] <= y;
    if (reset) begin
      for (i = 0; i < 4; i = i + 1) S[i] <= 16'd0;
    end else begin
      if (we0) S[a][7:0] <= x;
      if (we1) S[b][15:8] <= y;
    end
  end
  reg [15:0] p_after, r_after, s_after;
  always @* begin
    p_after = P[ra];
    if (we0 && a == ra) p_after[7:0]  = x;
    if (we1 && b == ra) p_after[15:8] = y;
    r_after = R[ra];
    if (we0 && a == ra) r_after[k] = bv;
    s_after = S[ra];
    if (!reset && we0 && a == ra) s_after[7:0]  = x;
    if (!reset && we1 && b == ra) s_after[15:8] = y;
  end
  assign qp = p_after;
  assign qo = O[ra];
  assign qr = r_after;
  assign qs = s_after;
endmodule
EOF

# 1. Structure: masked ports, lane widths, no old-value read ports.
"$LHD" compile "$W/mw.prp" --emit-dir "verilog:$W/v" --workdir "$W/wc" >"$W/c.log" 2>&1 \
  || { tail -5 "$W/c.log"; fail "the design did not compile (an inline reg array with lanes must forward per lane)"; }
V=$(cat "$W"/v/*.v)
[ "$(grep -c 'cgen_memory_1rd_2wr #' <<<"$V")" -ge 2 ] \
  || fail "case 1: P and O must be 1-read 2-write memories (a partial write needs no old-value read)"
grep -q 'WENSIZE(2)' <<<"$V" || fail "case 1: byte-lane partial writes must give wensize 2"
grep -q 'WENSIZE(16)' <<<"$V" || fail "case 1: a runtime-position write must give single-bit lanes (wensize 16)"
grep -q '_data\[a\]\[7:0\] <=' <<<"$V" || fail "case 1: the reset array S must write its low lane alone"
echo "ok: partial writes are write-masked ports (wensize 2 / 16), no old-value read ports"

# 2. The design against its golden.
OUT=$("$LHD" lec --ref "verilog:$W/gold.v" --impl "$W/mw.prp" --top mw --workdir "$W/l1" 2>&1); RC=$?
[ "$RC" -eq 0 ] || { echo "$OUT" | grep -aoE "first divergence at [^\"]{0,200}" | head -1; fail "case 2: not equivalent to the golden (rc=$RC)"; }
echo "ok: LEC-equivalent to the per-bit merging golden"

# 3. The mutant (the old last-lane-only bug) must refute.
cat > "$W/ms.prp" <<'EOF'
pub mod ms(`reset`:Reset, we0:Bool, we1:Bool, a:U2, b:U2, x:U8, y:U8, ra:U2) -> (qs:U16@[0]) {
  reg S:[4]U16 = 0
  if we0 { S[a]#[0..<8] = x }
  if we1 { S[b]#[8..<16] = y }
  qs = S[ra]
}
EOF
cat > "$W/mutant.v" <<'EOF'
module ms(input clock, input reset, input we0, input we1, input [1:0] a, input [1:0] b, input [7:0] x, input [7:0] y,
          input [1:0] ra, output [15:0] qs);
  reg [15:0] S[0:3];
  integer i;
  always @(posedge clock) begin
    if (reset) begin
      for (i = 0; i < 4; i = i + 1) S[i] <= 16'd0;
    end else begin
      if (we0 && !(we1 && a == b)) S[a][7:0] <= x;  // MUTANT: the high lane wipes the low one
      if (we1) S[b][15:8] <= y;
    end
  end
  reg [15:0] s_after;
  always @* begin
    s_after = S[ra];
    if (!reset && we0 && a == ra && !(we1 && b == ra)) s_after[7:0] = x;
    if (!reset && we1 && b == ra) s_after[15:8] = y;
  end
  assign qs = s_after;
endmodule
EOF
OUT=$("$LHD" lec --ref "verilog:$W/mutant.v" --impl "$W/ms.prp" --top ms --workdir "$W/l2" 2>&1); RC=$?
echo "$OUT" | grep -q '"verdict":"refuted"' || fail "case 3: the last-lane-only mutant was not refuted (rc=$RC)"
echo "ok: the last-lane-only mutant is refuted"

echo "PASS"
