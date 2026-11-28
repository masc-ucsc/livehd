#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# An asynchronous reset rung that sets every entry of a memory to a constant
# is extracted as the memory's asynchronous reset (reset contents, reset pin,
# polarity), not demoted to a synchronous reset. A demoted reset clears the
# memory only at the next clock edge, so the cycle the reset is asserted reads
# stale contents, and the exported certificate had no asynchronous reset image.
# Native LEC observes state only after clock updates and cannot tell the two
# apart, so this test pins the structure (no demotion warning, the Pyrope and
# Verilog spellings, the certificate's asyncImg); the optional event-level leg
# (LHD_EXTERNAL_SIM=1) checks the values between clock edges.

set -u

LHD=${LHD:-lhd/lhd}
SRC=inou/slang/tests/sv/async_reset_memory.v
TOP=async_reset_memory
W="${TEST_TMPDIR:-$(mktemp -d)}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  [ -f "$W/prp/$TOP.prp" ] && cat "$W/prp/$TOP.prp" >&2
  [ -f "$W/diag.jsonl" ] && cat "$W/diag.jsonl" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" \
  --emit-dir pyrope:"$W/prp" --emit verilog:"$W/out.v" --emit diagnostics:"$W/diag.jsonl" \
  --emit-dir lean:"$W/lean" --set formal.lean.mode=verified_compiler \
  --workdir "$W/work" -q >"$W/compile.log" 2>&1 \
  || { cat "$W/compile.log" >&2; fail "compile failed"; }

PRP="$W/prp/$TOP.prp"
[ -s "$PRP" ] || fail "Pyrope unit was not emitted"
[ -s "$W/out.v" ] || fail "Verilog was not emitted"
if grep -q 'async-reset-as-sync' "$W/diag.jsonl" "$W/compile.log" 2>/dev/null; then
  fail "an extractable asynchronous memory reset was demoted to a synchronous one"
fi

# The memory carries the reset the way a Pyrope register array spells it: the
# reset contents as its declaration value (per entry, from the loop variable,
# and from the element list), the rung's reset pin and its polarity.
grep -Eq 'reg m:\[4\]U8:\[.*reset_pin=rst_n, async=true, negreset=true.*\] = \(48, 49, 50, 51\)' "$PRP" \
  || fail "the for-loop reset of m is not m's asynchronous active-low reset"
grep -Eq 'reg n:\[4\]U4:\[.*reset_pin=rst, async=true[],].*\] = \(1, 2, 4, 8\)' "$PRP" \
  || fail "the element-list reset of n is not n's asynchronous active-high reset"
grep -q 'reg c:U8:\[initial=165, reset_pin=rst, async=true\]' "$PRP" \
  || fail "the register in the same rung lost its asynchronous reset"

# Both memories keep an asynchronous event and reload every entry in the arm.
[ "$(grep -c 'always @(posedge clk or posedge' "$W/out.v")" -ge 3 ] \
  || fail "the emitted Verilog lost an asynchronous reset event"
for v in "8'h30" "8'h31" "8'h32" "8'h33" "4'h1" "4'h2" "4'h4" "4'h8"; do
  grep -q "<= $v;" "$W/out.v" || fail "the emitted Verilog does not reset an entry to $v"
done

# The certificate states both resets as asynchronous reset images.
[ "$(grep -c 'asyncImg := some' "$W/lean/${TOP}_Lgraph.lean")" -eq 2 ] \
  || fail "the certificate does not carry an asynchronous reset image for each memory"

# The emitted Verilog and the Pyrope round trip stay equivalent to the source.
"$LHD" lec --impl verilog:"$W/out.v" --ref verilog:"$SRC" --top "$TOP" --workdir "$W/lec" -q >"$W/lec.log" 2>&1 \
  || { cat "$W/lec.log" >&2; fail "the emitted Verilog is not equivalent to the source"; }
"$LHD" compile "$PRP" --top "$TOP" --emit verilog:"$W/roundtrip.v" --workdir "$W/rt" -q >"$W/rt.log" 2>&1 \
  || { cat "$W/rt.log" >&2; fail "the emitted Pyrope did not recompile"; }
grep -q 'always @(posedge clk or posedge' "$W/roundtrip.v" || fail "the Pyrope round trip lost the asynchronous reset"
"$LHD" lec --impl verilog:"$W/roundtrip.v" --ref verilog:"$SRC" --top "$TOP" --workdir "$W/lec_rt" -q >"$W/lec_rt.log" 2>&1 \
  || { cat "$W/lec_rt.log" >&2; fail "the Pyrope round trip is not equivalent to the source"; }

# What the memory reset cannot express keeps the previous lowering: a reset arm
# that leaves an entry out (it would have to hold) still demotes with the
# warning, and still compiles.
cat >"$W/partial.v" <<'SV'
module partial(input clk, rst_n, we, input [1:0] wa, ra, input [7:0] wd, output [7:0] rd);
  reg [7:0] m [0:3];
  integer k;
  always @(posedge clk or negedge rst_n)
    if (!rst_n) begin for (k = 0; k < 3; k = k + 1) m[k] <= 8'h00; end
    else if (we) m[wa] <= wd;
  assign rd = m[ra];
endmodule
SV
"$LHD" compile "$W/partial.v" --reader slang --top partial --emit verilog:"$W/partial_out.v" \
  --emit diagnostics:"$W/partial.jsonl" --workdir "$W/partial" -q >/dev/null 2>&1 \
  || fail "a partially reset memory no longer compiles"
grep -q 'async-reset-as-sync' "$W/partial.jsonl" || fail "a partially reset memory was not demoted with the warning"

# A memory reset in a LATER rung runs only while the earlier guard is inactive
# (here the memory keeps its contents while arst is held), which one memory
# reset pin cannot state. It stays refused: the earlier rung already peeled, so
# demotion is not sound either (gate 1 above demote_reset_edges).
cat >"$W/later_rung.v" <<'SV'
module later_rung(input clk, arst, brst, we, input [1:0] wa, ra, input [7:0] wd, output [7:0] rd,
                  output reg [7:0] q);
  reg [7:0] m [0:3];
  integer k;
  always @(posedge clk or posedge arst or posedge brst)
    if (arst) q <= 8'h11;
    else if (brst) begin for (k = 0; k < 4; k = k + 1) m[k] <= 8'h22; end
    else begin q <= wd; if (we) m[wa] <= wd; end
  assign rd = m[ra];
endmodule
SV
if "$LHD" compile "$W/later_rung.v" --reader slang --top later_rung --emit verilog:"$W/later_rung_out.v" \
  --emit diagnostics:"$W/later_rung.jsonl" --workdir "$W/later_rung" -q >/dev/null 2>&1; then
  fail "a memory reset shadowed by an earlier asynchronous rung was accepted"
fi
grep -q 'unsupported-async-load' "$W/later_rung.jsonl" || fail "the later-rung memory reset was not refused by name"

# Initial-block contents follow the register rule: a memory has ONE initial
# value, so contents equal to the reset contents warn, different ones are refused.
for kind in same diff; do
  init=05
  [ "$kind" = same ] || init=06
  cat >"$W/init_$kind.v" <<SV
module init_$kind(input clk, rst_n, we, input [1:0] wa, ra, input [7:0] wd, output [7:0] rd);
  reg [7:0] m [0:3];
  integer k;
  initial for (k = 0; k < 4; k = k + 1) m[k] = 8'h$init;
  always @(posedge clk or negedge rst_n)
    if (!rst_n) begin for (k = 0; k < 4; k = k + 1) m[k] <= 8'h05; end
    else if (we) m[wa] <= wd;
  assign rd = m[ra];
endmodule
SV
  rc=0
  "$LHD" compile "$W/init_$kind.v" --reader slang --top "init_$kind" --emit verilog:"$W/init_${kind}_out.v" \
    --emit diagnostics:"$W/init_$kind.jsonl" --workdir "$W/init_$kind" -q >/dev/null 2>&1 || rc=$?
  if [ "$kind" = same ]; then
    [ "$rc" -eq 0 ] || fail "initial contents equal to the reset contents were refused"
    grep -q 'reg-init-and-reset' "$W/init_$kind.jsonl" || fail "equal initial and reset contents did not warn"
  else
    [ "$rc" -ne 0 ] || fail "initial contents different from the reset contents were accepted"
    grep -q 'reset-init-mismatch' "$W/init_$kind.jsonl" || fail "the initial/reset contents clash was not named"
  fi
done

if [ -n "${LHD_EXTERNAL_SIM:-}" ]; then
  for tool in iverilog vvp; do
    command -v "$tool" >/dev/null 2>&1 || fail "LHD_EXTERNAL_SIM is set but $tool is not on PATH"
  done
  # Event level: the emitted Verilog against the source, before and after each
  # edge, with each reset asserted between edges, held across an edge (with
  # the write enable high) and released.
  sed "s/module $TOP/module reference_memory/" "$SRC" >"$W/reference.v"
  cat >"$W/tb.v" <<'SV'
module tb;
  reg clk = 0, rst_n = 1, rst = 0, we = 0;
  reg [1:0] wa = 0, ra = 0;
  reg [7:0] wd = 0;
  wire [7:0] rd_m, gold_rd_m, cnt, gold_cnt;
  wire [3:0] rd_n, gold_rd_n;
  wire [31:0] all_m, gold_all_m;
  wire [15:0] all_n, gold_all_n;
  async_reset_memory dut(.*);
  reference_memory gold(.clk(clk), .rst_n(rst_n), .rst(rst), .we(we), .wa(wa), .wd(wd), .ra(ra),
                        .rd_m(gold_rd_m), .rd_n(gold_rd_n), .all_m(gold_all_m), .all_n(gold_all_n), .cnt(gold_cnt));
  task check(input integer k, input integer where);
    if ({rd_m, rd_n, all_m, all_n, cnt} !== {gold_rd_m, gold_rd_n, gold_all_m, gold_all_n, gold_cnt})
      $fatal(1, "step %0d (%0d): %h %h %h %h %h vs %h %h %h %h %h", k, where, rd_m, rd_n, all_m, all_n, cnt,
             gold_rd_m, gold_rd_n, gold_all_m, gold_all_n, gold_cnt);
  endtask
  initial begin
    #1 rst_n = 0; rst = 1; we = 1; wd = 8'hee;  // both resets asserted between edges
    for (integer k = 0; k < 48; k = k + 1) begin
      #1 check(k, 0);
      clk = 1;
      #1 check(k, 1);
      clk = 0;
      rst_n = !(k % 16 == 6 || k % 16 == 7 || k % 16 == 13);
      rst   = k % 16 == 9 || k % 16 == 10 || k % 16 == 14;
      we = (k % 3) != 0 || !rst_n || rst;
      wa = k * 7 % 4; ra = k * 5 % 4; wd = k * 37 + 11;
      #1 check(k, 2);
    end
    $finish;
  end
endmodule
SV
  iverilog -g2012 -I ware/rtl -s tb -o "$W/event_sim" "$W/out.v" "$W/reference.v" "$W/tb.v" \
    || fail "event-level bench did not build"
  vvp -n "$W/event_sim" || fail "the emitted Verilog differs from the source between clock edges"
else
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
fi

echo "PASS: async_reset_memory"
