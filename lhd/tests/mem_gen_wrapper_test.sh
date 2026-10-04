#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The GENERATED memory wrapper, simulated.
#
# ware/rtl ships a fixed wrapper family; for any other (R,W,clock) shape
# Cgen_verilog GENERATES the module inline (cgen_verilog.cpp:1305 -- single
# clock accepts 1..4 reads with 1..2 writes, plus 1rd_3wr and 1rd_4wr, and
# nothing else). That generated template is a SECOND copy of the memory
# semantics, so a fix applied only to ware/rtl would leave every unshipped
# shape wrong.
#
# mem_sync_read_hold_test could only grep the template's source text. This
# runs the real thing: gen_mem_wrapper_dump calls the actual private
# Cgen_verilog::gen_mem_wrapper through one named friend peer, and the emitted
# module is simulated with the same exact-value stimulus the shipped
# multi-read test uses.
#
# SCOPE, stated honestly: this covers the GENERATOR's output for a shape it is
# asked for. It does NOT cover shape DISPATCH -- that `have_wrapper` routes a
# 2rd_3wr design here rather than to an `include` is read from the source
# above, not exercised, because no RTL in this suite produces that shape.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
DUMP=""
for c in "${TEST_SRCDIR:-}/_main/lhd/tests/gen_mem_wrapper_dump" \
         "$ROOT/bazel-bin/lhd/tests/gen_mem_wrapper_dump" \
         "bazel-bin/lhd/tests/gen_mem_wrapper_dump"; do
  [ -x "$c" ] && { DUMP="$c"; break; }
done
if [ -z "$DUMP" ]; then
  echo "SKIP-REASON: gen_mem_wrapper_dump not in runfiles"
  echo "SKIPPED: mem_gen_wrapper_test -- no case executed. NOT acceptance."
  exit 0
fi
if ! command -v iverilog >/dev/null 2>&1 || ! command -v vvp >/dev/null 2>&1; then
  echo "SKIP-REASON: iverilog=$(command -v iverilog || echo MISSING) vvp=$(command -v vvp || echo MISSING)"
  echo "SKIPPED: mem_gen_wrapper_test -- no case executed. NOT acceptance."
  exit 0
fi

W="${TEST_TMPDIR:-$ROOT/generated/tests}/mem_gen_wrapper"
rm -rf "$W"; mkdir -p "$W"
rc_all=0

# 2rd_3wr, single clock: have_wrapper is false for it, so this is a shape the
# generator really owns.
"$DUMP" cgen_memory_2rd_3wr 2 3 1 > "$W/gen.v"; drc=$?
echo "generator exit=$drc, $(wc -l < "$W/gen.v") lines"
[ "$drc" -eq 0 ] || { echo "FAIL: the generator exited $drc"; exit 1; }
grep -q "^module cgen_memory_2rd_3wr" "$W/gen.v" \
  || { echo "FAIL: the output does not define the requested module"; exit 1; }
for p in rd_enable_0 rd_enable_1 wr_enable_0 wr_enable_1 wr_enable_2; do
  grep -q "$p" "$W/gen.v" || { echo "FAIL: generated module lacks $p -- wrong shape"; exit 1; }
done
echo "ok: generated module names the requested 2rd_3wr shape with all its ports"

# FWD bit (k*n_wr + j): port0<-write0 is bit 0, port1<-write0 is bit 3 with
# n_wr=3. 0b001001 = 9 forwards write port 0 to BOTH read ports.
cat > "$W/tb.v" <<'EOF'
`timescale 1ns/1ps
module tb;
  reg clk = 0; integer errs = 0;
  reg  [2:0] a0 = 0, a1 = 0, wa = 0;
  reg        e0 = 0, e1 = 0, we = 0;
  reg  [7:0] wd = 0;
  wire [7:0] d0, d1, f0, f1;
  always #5 clk = ~clk;
  // Write ports 1 and 2 tied INACTIVE so this is the same experiment the
  // shipped 2rd_1wr test runs.
  cgen_memory_2rd_3wr #(.BITS(8), .SIZE(8), .WENSIZE(1), .FWD(0), .LATENCY_0(1),
                        .INIT_EN(1), .INIT(64'h8877665544332211)) u (
    .clk(clk), .rd_addr_0(a0), .rd_enable_0(e0), .rd_dout_0(d0),
               .rd_addr_1(a1), .rd_enable_1(e1), .rd_dout_1(d1),
               .wr_addr_0(wa), .wr_enable_0(we), .wr_din_0(wd),
               .wr_addr_1(3'b0), .wr_enable_1(1'b0), .wr_din_1(8'b0),
               .wr_addr_2(3'b0), .wr_enable_2(1'b0), .wr_din_2(8'b0));
  cgen_memory_2rd_3wr #(.BITS(8), .SIZE(8), .WENSIZE(1), .FWD(9), .LATENCY_0(1),
                        .INIT_EN(1), .INIT(64'h8877665544332211)) uf (
    .clk(clk), .rd_addr_0(a0), .rd_enable_0(e0), .rd_dout_0(f0),
               .rd_addr_1(a1), .rd_enable_1(e1), .rd_dout_1(f1),
               .wr_addr_0(wa), .wr_enable_0(we), .wr_din_0(wd),
               .wr_addr_1(3'b0), .wr_enable_1(1'b0), .wr_din_1(8'b0),
               .wr_addr_2(3'b0), .wr_enable_2(1'b0), .wr_din_2(8'b0));

  task step(input [2:0] A0, input E0, input [2:0] A1, input E1,
            input W, input [2:0] WA, input [7:0] WD);
    begin
      @(negedge clk); a0=A0; e0=E0; a1=A1; e1=E1; we=W; wa=WA; wd=WD;
      @(posedge clk); #1;
    end
  endtask
  task chk(input [8*24:1] nm, input [7:0] got, input [7:0] want);
    begin
      if (got !== want) begin
        errs = errs + 1;
        $display("MISMATCH %0s: got %02h want %02h", nm, got, want);
      end
    end
  endtask

  initial begin
    step(3'd0,1,3'd1,1, 0,0,0);  chk("c1 port0", d0, 8'h11); chk("c1 port1", d1, 8'h22);
    step(3'd3,0,3'd4,1, 0,0,0);  chk("c2 port0 hold", d0, 8'h11); chk("c2 port1 read", d1, 8'h55);
    step(3'd5,1,3'd6,0, 0,0,0);  chk("c3 port0 read", d0, 8'h66); chk("c3 port1 hold", d1, 8'h55);
    step(3'd7,0,3'd0,0, 0,0,0);  chk("c4 port0 hold", d0, 8'h66); chk("c4 port1 hold", d1, 8'h55);
    step(3'd7,1,3'd0,1, 0,0,0);  chk("c5 port0 reenable", d0, 8'h88); chk("c5 port1 reenable", d1, 8'h11);
    step(3'd2,1,3'd2,1, 1,3'd2,8'hAA);
    chk("c6 fwd port0", f0, 8'hAA); chk("c6 fwd port1", f1, 8'hAA);
    chk("c6 nofwd port0", d0, 8'h33); chk("c6 nofwd port1", d1, 8'h33);
    step(3'd2,1,3'd2,1, 0,0,0);
    chk("c7 committed port0", d0, 8'hAA); chk("c7 committed port1", d1, 8'hAA);
    if (errs == 0) $display("GENWRAP_OK"); else $display("GENWRAP_FAIL errs=%0d", errs);
    $finish;
  end
endmodule
EOF

run_tb() {  # <wrapper.v> -> OUT / RC
  iverilog -g2012 -o "$W/tb.vvp" "$1" "$W/tb.v" >"$W/iv.log" 2>&1 || {
    OUT="$(tail -6 "$W/iv.log")"; RC=99; return; }
  OUT="$(vvp "$W/tb.vvp" 2>&1)"; RC=$?
}

echo "--- the GENERATED wrapper must satisfy every case ---"
run_tb "$W/gen.v"
echo "$OUT" | sed 's/^/    /'
echo "    (iverilog+vvp exit=$RC)"
if [ "$RC" -ne 0 ] || ! echo "$OUT" | grep -q "GENWRAP_OK"; then
  echo "FAIL: the generated 2rd_3wr wrapper does not meet the hold contract"
  rc_all=1
else
  echo "ok: generated wrapper -- per-port hold, re-enable, FWD/no-FWD all exact"
fi

# Negative controls mutate a DIAGNOSTIC COPY OF THE GENERATED OUTPUT, never
# the generator source: this asks whether the test can see a wrong generated
# wrapper, which is the property at issue.
neg() {  # <name> <sed-program> <expect-substring>
  sed "$2" "$W/gen.v" > "$W/mut.v"
  cmp -s "$W/mut.v" "$W/gen.v" && { echo "FAIL: '$1' did not apply"; rc_all=1; return; }
  run_tb "$W/mut.v"
  [ "$RC" -eq 99 ] && { echo "FAIL: '$1' did not BUILD; not a semantic kill"
                        echo "$OUT" | sed 's/^/      /'; rc_all=1; return; }
  echo "$OUT" | grep -q "GENWRAP_OK" && { echo "FAIL: '$1' SURVIVED"; rc_all=1; return; }
  echo "ok: '$1' killed semantically (vvp exit=$RC) -- $(echo "$OUT" | grep -m1 MISMATCH)"
  echo "$OUT" | grep -q "$3" \
    || { echo "FAIL: '$1' failed, but not at the expected case ($3)"; rc_all=1; }
}
echo "--- negative controls, on a copy of the GENERATED output ---"
neg "port 1 registered read made unconditional" \
    's/if (rd_enable_1) rd_dout_1 <=/rd_dout_1 <=/' "c3 port1 hold"
neg "port 1 gated by port 0's enable" \
    's/if (rd_enable_1) rd_dout_1 <=/if (rd_enable_0) rd_dout_1 <=/' "port1"

[ "$rc_all" -eq 0 ] || { echo "FAIL: mem_gen_wrapper_test"; exit 1; }
echo "PASS: mem_gen_wrapper_test (7 cases + 2 negative controls, all EXECUTED)"
