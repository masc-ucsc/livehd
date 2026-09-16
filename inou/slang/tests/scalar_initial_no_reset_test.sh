#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/scalar_initial_no_reset.v
TOP=scalar_initial_no_reset
W="${TEST_TMPDIR:-/tmp/scalar_initial_no_reset_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  [ -f "$W/prp/$TOP.prp" ] && sed -n '1,80p' "$W/prp/$TOP.prp" >&2
  [ -f "$W/diag.jsonl" ] && sed -n '1,80p' "$W/diag.jsonl" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" \
  --emit-dir pyrope:"$W/prp" --emit verilog:"$W/out.v" \
  --emit diagnostics:"$W/diag.jsonl" --workdir "$W/work" -q >/dev/null 2>&1 \
  || fail "compile failed"

PRP="$W/prp/$TOP.prp"
[ -s "$PRP" ] || fail "Pyrope unit was not emitted"
[ -s "$W/out.v" ] || fail "Verilog was not emitted"
[ -s "$W/diag.jsonl" ] || fail "diagnostics were not emitted"

# A declaration initializer and a simple initial-block assignment both become
# the register declaration's concrete initializer. tolg turns that into the
# module's implicit reset rather than leaving the register's power-up state X.
grep -q 'reg decl_q:u8 = 0xa5' "$PRP" \
  || fail "declaration initializer was not preserved as the reset value"
grep -q 'reg block_q:u8 = 60' "$PRP" \
  || fail "initial-block assignment was not preserved as the reset value"
grep -Eq 'decl_q <= .*a5' "$W/out.v" \
  || fail "declaration-initialized register has no generated reset assignment"
grep -Eq 'block_q <= .*3c' "$W/out.v" \
  || fail "initial-block register has no generated reset assignment"

# An explicit source reset is preserved as the register's one initial value, and
# such a register must not receive the no-reset warning.
grep -q 'reg reset_q:u8:\[initial=17, reset_pin=ref rst, async=true\]' "$PRP" \
  || fail "explicit reset value was not preserved"
grep -Eq 'reset_q <= .*11' "$W/out.v" \
  || fail "explicit reset assignment was replaced by the declaration initializer"
grep -q 'initial-without-reset.*reset_q' "$W/diag.jsonl" \
  && fail "explicitly reset register received the no-reset warning"

COUNT=$(grep -c '"code":"initial-without-reset"' "$W/diag.jsonl")
[ "$COUNT" -eq 2 ] || fail "expected two initial-without-reset warnings, got $COUNT"
grep -q 'formal equivalence may otherwise differ from reset-less hardware' "$W/diag.jsonl" \
  || fail "warning did not explain the LEC risk"

# A register has ONE initial value: its power-on value IS its reset value (they
# ride the same `initial` pin, the rule upass.tolg already enforces for a reg
# ARRAY). A declaration initializer that DISAGREES with an explicit reset value
# cannot be represented, and neither silent outcome was acceptable -- the async
# spelling dropped the power-on value while the synchronous one reset to it
# instead of to the source's reset value. Refuse the contradiction.
cat >"$W/reset_init_mismatch.v" <<'SV'
module reset_init_mismatch(input clk, rst, input [7:0] d, output reg [7:0] q);
  reg [7:0] r = 8'hA5;                   // power-on 0xA5 ...
  always @(posedge clk or posedge rst) begin
    if (rst) r <= 8'h00;                 // ... but reset to 0x00
    else r <= d;
  end
  always @(posedge clk) q <= r;
endmodule
SV
"$LHD" compile "$W/reset_init_mismatch.v" --reader slang --top reset_init_mismatch \
  --workdir "$W/mismatch-work" >"$W/mismatch.log" 2>&1 \
  && fail "a declaration initializer that disagrees with the reset value was accepted"
grep -q '"code":"reset-init-mismatch"' "$W/mismatch.log" \
  || { sed -n '1,40p' "$W/mismatch.log" >&2; fail "the contradiction was not reported as reset-init-mismatch"; }
grep -q '"severity":"error"' "$W/mismatch.log" \
  || fail "the reset/init contradiction was not an error"
grep -q 'initial-without-reset' "$W/mismatch.log" \
  && fail "a register with an explicit reset also got the no-reset warning"

# The SAME value in both places is one register spelled twice, not a clash.
cat >"$W/reset_init_agree.v" <<'SV'
module reset_init_agree(input clk, rst, input [7:0] d, output reg [7:0] q);
  reg [7:0] r = 8'hA5;
  always @(posedge clk or posedge rst) begin
    if (rst) r <= 8'hA5;
    else r <= d;
  end
  always @(posedge clk) q <= r;
endmodule
SV
"$LHD" compile "$W/reset_init_agree.v" --reader slang --top reset_init_agree \
  --workdir "$W/agree-work" >"$W/agree.log" 2>&1 \
  || { sed -n '1,40p' "$W/agree.log" >&2; fail "a declaration initializer equal to the reset value was refused"; }
grep -q 'reset-init-mismatch' "$W/agree.log" \
  && fail "matching power-on and reset values were reported as a contradiction"

# ── synchronous reset recognition ────────────────────────────────────────────
# `always_ff @(posedge clk) if (rst) q <= C; else q <= d;` is a RESET, not
# ordinary clocked logic. It carries no extra edge trigger, so it used to lower
# as a data-path mux: the register had no reset in LiveHD's vocabulary at all,
# which is what let the contradiction above go undiagnosed on this spelling.
cat >"$W/sync_reset.v" <<'SV'
module sync_reset(input clk, rst, rst_n, enable, input [7:0] d, e,
                  output reg [7:0] q, p, lo, en_q, dyn);
  always @(posedge clk) begin                 // recognized: two regs, one arm
    if (rst) begin q <= 8'h00; p <= 8'h3C; end
    else     begin q <= d;     p <= q;     end
  end
  always @(posedge clk) begin                 // recognized: active low
    if (!rst_n) lo <= 8'h2A;
    else        lo <= d;
  end
  always @(posedge clk) begin                 // NOT a reset: not a reset name
    if (enable) en_q <= 8'h00;
    else        en_q <= d;
  end
  always @(posedge clk) begin                 // NOT a reset: runtime value
    if (rst) dyn <= e;
    else     dyn <= d;
  end
endmodule
SV
"$LHD" compile "$W/sync_reset.v" --reader slang --top sync_reset \
  --emit-dir pyrope:"$W/sync_prp" --workdir "$W/sync-work" -q >/dev/null 2>&1 \
  || fail "the synchronous-reset fixture did not compile"
SP="$W/sync_prp/sync_reset.prp"
grep -q 'reg q:u8:\[initial=0, reset_pin=ref rst, async=false\]' "$SP" \
  || fail "a synchronous `if (rst)` guard was not recognized as a reset"
grep -q 'reg p:u8:\[initial=60, reset_pin=ref rst, async=false\]' "$SP" \
  || fail "the second register of the same reset arm was not recognized"
grep -q 'reg lo:u8:\[initial=42, reset_pin=ref rst_n, async=false, negreset=true\]' "$SP" \
  || fail "an active-low synchronous reset lost its polarity"
# The guard must stay narrow: a reset is recognized by the SHARED reset-name
# token test, and its value must be constant. Anything else is ordinary logic,
# and reclassifying it would put a functional enable on the design's reset pin.
grep -q 'reg en_q:u8$' "$SP" \
  || fail "an enable-guarded register was reclassified as reset-bearing: $(grep 'reg en_q' "$SP")"
grep -q 'reg dyn:u8$' "$SP" \
  || fail "a runtime `if (rst) q <= e` value was treated as a reset value: $(grep 'reg dyn' "$SP")"

echo "PASS: scalar initial values become implicit reset values with a located warning"
